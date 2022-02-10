# -*- coding: utf-8 -*-
import sys
import itertools
import os
import tempfile
import socket
import six

from google.protobuf.text_format import Parse
from ydb.core.protos import config_pb2
import ydb.tests.library.common.yatest_common as yatest_common

from . import tls_tools
from ydb.tests.library.common.types import Erasure
from .kikimr_port_allocator import KikimrPortManagerPortAllocator
from .param_constants import kikimr_driver_path
from .util import LogLevels
import yaml
from library.python import resource


PDISK_SIZE_STR = os.getenv("YDB_PDISK_SIZE", str(64 * 1024 * 1024 * 1024))
if PDISK_SIZE_STR.endswith("GB"):
    PDISK_SIZE = int(PDISK_SIZE_STR[:-2]) * 1024 * 1024 * 1024
else:
    PDISK_SIZE = int(PDISK_SIZE_STR)


def get_fqdn():
    hostname = socket.gethostname()
    addrinfo = socket.getaddrinfo(hostname, None, socket.AF_UNSPEC, 0, 0, socket.AI_CANONNAME)
    for ai in addrinfo:
        canonname_candidate = ai[3]
        if canonname_candidate:
            return six.text_type(canonname_candidate)
    assert False, 'Failed to get FQDN'


def get_additional_log_configs():
    log_configs = os.getenv('YDB_ADDITIONAL_LOG_CONFIGS', '')
    rt = {}

    for c_and_l in log_configs.split(','):
        if c_and_l:
            c, l = c_and_l.split(':')
            rt[c] = LogLevels.from_string(l)
    return rt


def get_grpc_host():
    if sys.platform == "darwin":
        return "localhost"
    return "[::]"


def load_default_yaml(default_tablet_node_ids, ydb_domain_name, static_erasure, n_to_select, state_storage_nodes, log_configs):
    data = resource.find("harness/resources/default_yaml.yml")
    if isinstance(data, bytes):
        data = data.decode('utf-8')
    data = data.format(
        ydb_result_rows_limit=os.getenv("YDB_KQP_RESULT_ROWS_LIMIT", 1000),
        ydb_yql_syntax_version=os.getenv("YDB_YQL_SYNTAX_VERSION", "1"),
        ydb_defaut_tablet_node_ids=str(default_tablet_node_ids),
        ydb_default_log_level=int(LogLevels.from_string(os.getenv("YDB_DEFAULT_LOG_LEVEL", "NOTICE"))),
        ydb_domain_name=ydb_domain_name,
        ydb_static_erasure=static_erasure,
        ydb_state_storage_n_to_select=n_to_select,
        ydb_state_storage_nodes=state_storage_nodes,
        ydb_grpc_host=get_grpc_host(),
    )
    yaml_dict = yaml.safe_load(data)
    yaml_dict["log_config"]["entry"] = []
    for log, level in six.iteritems(log_configs):
        yaml_dict["log_config"]["entry"].append({"component": log, "level": int(level)})
    return yaml_dict


class KikimrConfigGenerator(object):
    def __init__(
            self,
            erasure=None,
            binary_path=None,
            nodes=None,
            additional_log_configs=None,
            port_allocator=None,
            has_cluster_uuid=True,
            load_udfs=False,
            udfs_path=None,
            output_path=None,
            enable_pq=False,
            slot_count=0,
            pdisk_store_path=None,
            version=None,
            enable_nbs=False,
            enable_sqs=False,
            domain_name='Root',
            suppress_version_check=True,
            static_pdisk_size=PDISK_SIZE,
            dynamic_pdisk_size=PDISK_SIZE,
            dynamic_pdisks=[],
            dynamic_storage_pools=[dict(name="dynamic_storage_pool:1", kind="hdd", pdisk_user_kind=0)],
            n_to_select=None,
            use_log_files=True,
            grpc_ssl_enable=False,
            use_in_memory_pdisks=False,
            enable_pqcd=False, 
            enable_metering=False,
            grpc_tls_data_path=None,
            yql_config_path=None,
            enable_datastreams=False,
            auth_config_path=None,
            disable_mvcc=False,
            enable_public_api_external_blobs=False,
    ):
        self._version = version
        self.use_log_files = use_log_files
        self.suppress_version_check = suppress_version_check
        self._pdisk_store_path = pdisk_store_path
        self.static_pdisk_size = static_pdisk_size
        self.app_config = config_pb2.TAppConfig()
        self.port_allocator = KikimrPortManagerPortAllocator() if port_allocator is None else port_allocator
        erasure = Erasure.NONE if erasure is None else erasure
        self.__grpc_ssl_enable = grpc_ssl_enable
        self.__grpc_tls_data_path = None
        self.__grpc_ca_file = None
        self.__grpc_cert_file = None
        self.__grpc_key_file = None
        self.__grpc_tls_ca = None
        self.__grpc_tls_key = None
        self.__grpc_tls_cert = None
        self._pdisks_info = []
        if self.__grpc_ssl_enable:
            self.__grpc_tls_data_path = grpc_tls_data_path or yatest_common.output_path()
            cert_pem, key_pem = tls_tools.generate_selfsigned_cert(get_fqdn())
            self.__grpc_tls_ca = cert_pem
            self.__grpc_tls_key = key_pem
            self.__grpc_tls_cert = cert_pem

        if binary_path is None:
            binary_path = kikimr_driver_path()
        self.__binary_path = binary_path
        rings_count = 3 if erasure == Erasure.MIRROR_3_DC else 1
        if nodes is None:
            nodes = rings_count * erasure.min_fail_domains
        self._rings_count = rings_count
        self._enable_nbs = enable_nbs
        self.__node_ids = list(range(1, nodes + 1))
        self.n_to_select = n_to_select
        if self.n_to_select is None:
            if erasure == Erasure.MIRROR_3_DC:
                self.n_to_select = 9
            else:
                self.n_to_select = min(5, nodes)
        self.__use_in_memory_pdisks = use_in_memory_pdisks or os.getenv('YDB_USE_IN_MEMORY_PDISKS') == 'true'
        self.static_erasure = erasure
        self.domain_name = domain_name
        self.__number_of_pdisks_per_node = 1 + len(dynamic_pdisks)
        self.__load_udfs = load_udfs
        self.__udfs_path = udfs_path
        self.__yql_config_path = yql_config_path
        self.__auth_config_path = auth_config_path
        self.__slot_count = slot_count
        self._dcs = [1]
        if erasure == Erasure.MIRROR_3_DC:
            self._dcs = [1, 2, 3]

        self.__additional_log_configs = {} if additional_log_configs is None else additional_log_configs
        self.__additional_log_configs.update(get_additional_log_configs())

        self.dynamic_pdisk_size = dynamic_pdisk_size
        self.dynamic_storage_pools = dynamic_storage_pools

        self.__dynamic_pdisks = dynamic_pdisks

        self.__output_path = output_path or yatest_common.output_path()

        self.yaml_config = load_default_yaml(self.__node_ids, self.domain_name, self.static_erasure, self.n_to_select, self.__node_ids, self.__additional_log_configs)
        self.yaml_config["feature_flags"]["enable_public_api_external_blobs"] = enable_public_api_external_blobs
        self.yaml_config["feature_flags"]["enable_mvcc"] = "VALUE_FALSE" if disable_mvcc else "VALUE_TRUE"
        self.yaml_config['pqconfig']['enabled'] = enable_pq
        self.yaml_config['pqconfig']['topics_are_first_class_citizen'] = enable_pq and enable_datastreams
        self.yaml_config['sqs_config']['enable_sqs'] = enable_sqs
        self.yaml_config['pqcluster_discovery_config']['enabled'] = enable_pqcd
        self.yaml_config["net_classifier_config"]["net_data_file_path"] = os.path.join(self.__output_path, 'netData.tsv')
        with open(self.yaml_config["net_classifier_config"]["net_data_file_path"], "w") as net_data_file:
            net_data_file.write("")

        if enable_metering:
            self.__set_enable_metering()

        self.naming_config = config_pb2.TAppConfig()
        dc_it = itertools.cycle(self._dcs)
        rack_it = itertools.count(start=1)
        body_it = itertools.count(start=1)
        self.yaml_config["nameservice_config"] = {"node": []}
        for node_id in self.__node_ids:
            dc, rack, body = next(dc_it), next(rack_it), next(body_it)
            ic_port = self.port_allocator.get_node_port_allocator(node_id).ic_port
            node = self.naming_config.NameserviceConfig.Node.add(
                NodeId=node_id,
                Address="::1",
                Port=ic_port,
                Host="localhost",
            )

            node.WalleLocation.DataCenter = str(dc)
            node.WalleLocation.Rack = str(rack)
            node.WalleLocation.Body = body
            self.yaml_config["nameservice_config"]["node"].append(
                dict(
                    node_id=node_id,
                    address="::1", port=ic_port,
                    host='localhost',
                    walle_location=dict(
                        data_center=str(dc),
                        rack=str(rack),
                        body=body,
                    )
                )
            )

        self.__build()

        if self.grpc_ssl_enable:
            self.yaml_config["grpc_config"]["ca"] = self.grpc_tls_ca_path
            self.yaml_config["grpc_config"]["cert"] = self.grpc_tls_cert_path
            self.yaml_config["grpc_config"]["key"] = self.grpc_tls_key_path

        if os.getenv("YDB_ALLOW_ORIGIN") is not None:
            self.yaml_config["monitoring_config"] = {"allow_origin": str(os.getenv("YDB_ALLOW_ORIGIN"))}

    @property
    def pdisks_info(self):
        return self._pdisks_info

    @property
    def grpc_tls_data_path(self):
        return self.__grpc_tls_data_path

    @property
    def grpc_tls_key_path(self):
        return os.path.join(self.grpc_tls_data_path, 'key.pem')

    @property
    def grpc_tls_cert_path(self):
        return os.path.join(self.grpc_tls_data_path, 'cert.pem')

    @property
    def grpc_tls_ca_path(self):
        return os.path.join(self.grpc_tls_data_path, 'ca.pem')

    @property
    def grpc_ssl_enable(self):
        return self.__grpc_ssl_enable

    @property
    def grpc_tls_cert(self):
        return self.__grpc_tls_cert

    @property
    def grpc_tls_key(self):
        return self.__grpc_tls_key

    @property
    def grpc_tls_ca(self):
        return self.__grpc_tls_ca

    @property
    def domains_txt(self):
        app_config = config_pb2.TAppConfig()
        Parse(resource.find("harness/resources/default_domains.txt"), app_config.DomainsConfig)
        return app_config.DomainsConfig

    @property
    def names_txt(self):
        return self.naming_config.NameserviceConfig

    def __set_enable_metering(self):
        def ensure_path_exists(path):
            if not os.path.isdir(path):
                os.makedirs(path)
            return path

        def get_cwd_for_test(output_path):
            test_name = yatest_common.context.test_name or ""
            test_name = test_name.replace(':', '_')
            return os.path.join(output_path, test_name)

        cwd = get_cwd_for_test(self.__output_path)
        ensure_path_exists(cwd)
        metering_file_path = os.path.join(cwd, 'metering.txt')
        with open(metering_file_path, "w") as metering_file:
            metering_file.write('')
        self.yaml_config['metering_config'] = {'metering_file_path': metering_file_path}

    @property
    def metering_file_path(self):
        return self.yaml_config.get('metering_config', {}).get('metering_file_path')

    @property
    def nbs_enable(self):
        return self._enable_nbs

    @property
    def sqs_service_enabled(self):
        return self.yaml_config['sqs_config']['enable_sqs']

    @property
    def output_path(self):
        return self.__output_path

    @property
    def binary_path(self):
        return self.__binary_path

    def write_tls_data(self):
        if self.__grpc_ssl_enable:
            for fpath, data in ((self.grpc_tls_ca_path, self.grpc_tls_ca), (self.grpc_tls_cert_path, self.grpc_tls_cert), (self.grpc_tls_key_path, self.grpc_tls_key)):
                with open(fpath, 'wb') as f:
                    f.write(data)

    def write_proto_configs(self, configs_path):
        self.write_tls_data()
        with open(os.path.join(configs_path, "config.yaml"), "w") as writer:
            writer.write(yaml.safe_dump(self.yaml_config))

        if self.__yql_config_path:
            config_file_path = os.path.join(configs_path, "yql.txt")
            with open(self.__yql_config_path, "r") as source_config:
                with open(config_file_path, "w") as config_file:
                    config_file.write(source_config.read())

        if self.__auth_config_path:
            config_file_path = os.path.join(configs_path, "auth.txt")
            with open(self.__auth_config_path, "r") as source_config:
                with open(config_file_path, "w") as config_file:
                    config_file.write(source_config.read())

    def get_yql_udfs_to_load(self):
        if not self.__load_udfs:
            return []
        udfs_path = self.__udfs_path or yatest_common.build_path("yql/udfs")
        result = []
        for dirpath, dnames, fnames in os.walk(udfs_path):
            for f in fnames:
                if f.endswith(".so"):
                    full = os.path.join(dirpath, f)
                    result.append(full)
                elif f.endswith('.dylib'):
                    full = os.path.join(dirpath, f)
                    result.append(full)
        return result

    def all_node_ids(self):
        return self.__node_ids

    def _add_pdisk_to_static_group(self, pdisk_id, path, node_id, pdisk_category, ring):
        domain_id = len(self.yaml_config['blob_storage_config']["service_set"]["groups"][0]["rings"][ring]["fail_domains"])
        self.yaml_config['blob_storage_config']["service_set"]["pdisks"].append(
            {"node_id": node_id, "pdisk_id": pdisk_id, "path": path, "pdisk_guid": pdisk_id, "pdisk_category": pdisk_category})
        self.yaml_config['blob_storage_config']["service_set"]["vdisks"].append(
            {
                "vdisk_id": {"group_id": 0, "group_generation": 1, "ring": ring, "domain": domain_id, "vdisk": 0},
                "vdisk_location": {"node_id": node_id, "pdisk_id": pdisk_id, "pdisk_guid": pdisk_id, 'vdisk_slot_id': 0},
            }
        )
        self.yaml_config['blob_storage_config']["service_set"]["groups"][0]["rings"][ring]["fail_domains"].append(
            {"vdisk_locations": [{"node_id": node_id, "pdisk_id": pdisk_id, "pdisk_guid": pdisk_id, 'vdisk_slot_id': 0}]}
        )

    def __build(self):
        datacenter_id_generator = itertools.cycle(self._dcs)
        self.yaml_config["blob_storage_config"] = {}
        self.yaml_config["blob_storage_config"]["service_set"] = {}
        self.yaml_config["blob_storage_config"]["service_set"]["availability_domains"] = 1
        self.yaml_config["blob_storage_config"]["service_set"]["pdisks"] = []
        self.yaml_config["blob_storage_config"]["service_set"]["vdisks"] = []
        self.yaml_config["blob_storage_config"]["service_set"]["groups"] = [{"group_id": 0, 'group_generation': 0, 'erasure_species': int(self.static_erasure)}]
        self.yaml_config["blob_storage_config"]["service_set"]["groups"][0]["rings"] = []

        for dc in self._dcs:
            self.yaml_config["blob_storage_config"]["service_set"]["groups"][0]["rings"].append({"fail_domains": []})

        for node_id in self.__node_ids:
            datacenter_id = next(datacenter_id_generator)

            for pdisk_id in range(1, self.__number_of_pdisks_per_node + 1):
                disk_size = self.static_pdisk_size if pdisk_id <= 1 else self.__dynamic_pdisks[pdisk_id - 2].get('disk_size', self.dynamic_pdisk_size)
                pdisk_user_kind = 0 if pdisk_id <= 1 else self.__dynamic_pdisks[pdisk_id - 2].get('user_kind', 0)

                if self.__use_in_memory_pdisks:
                    pdisk_size_gb = disk_size / (1024*1024*1024)
                    pdisk_path = "SectorMap:%d:%d" % (pdisk_id, pdisk_size_gb)
                else:
                    tmp_file = tempfile.NamedTemporaryFile(prefix="pdisk{}".format(pdisk_id), suffix=".data", dir=self._pdisk_store_path)
                    pdisk_path = tmp_file.name

                self._pdisks_info.append({'pdisk_path': pdisk_path, 'node_id': node_id, 'disk_size': disk_size, 'pdisk_user_kind': pdisk_user_kind})
                if pdisk_id == 1 and node_id <= self.static_erasure.min_fail_domains * self._rings_count:
                    self._add_pdisk_to_static_group(
                        pdisk_id,
                        pdisk_path,
                        node_id,
                        pdisk_user_kind,
                        datacenter_id - 1,
                    )
