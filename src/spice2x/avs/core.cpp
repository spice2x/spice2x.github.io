#include "core.h"

#include <mutex>
#include <optional>
#include <vector>

#include <stdint.h>

#include "external/robin_hood.h"
#include "launcher/logger.h"
#include "launcher/signal.h"
#include "util/deferlog.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"

namespace avs {

    namespace core {

        // functions
        AVS215_BOOT_T avs215_boot {};
        AVS216_BOOT_T avs216_boot {};
        AVS_SHUTDOWN_T avs_shutdown {};
        PROPERTY_CREATE_T property_create {};
        PROPERTY_DESC_TO_BUFFER_T property_desc_to_buffer {};
        PROPERTY_DESTROY_T property_destroy {};
        PROPERTY_INSERT_READ_T property_insert_read {};
        PROPERTY_NODE_CREATE_T property_node_create {};
        PROPERTY_NODE_REFER_T property_node_refer {};
        PROPERTY_NODE_REMOVE_T property_node_remove {};
        PROPERTY_READ_QUERY_MEMSIZE_T property_read_query_memsize {};
        PROPERTY_SEARCH_T property_search {};
        STD_SETENV_T avs_std_setenv {};

        // optional functions
        AVS_FS_OPEN_T avs_fs_open {};
        AVS_FS_CLOSE_T avs_fs_close {};
        AVS_FS_DUMP_MOUNTPOINT_T avs_fs_dump_mountpoint {};
        AVS_FS_MOUNT_T avs_fs_mount {};
        AVS_FS_COPY_T avs_fs_copy {};
        AVS_FS_FSTAT_T avs_fs_fstat {};
        AVS_FS_LSTAT_T avs_fs_lstat {};
        AVS_FS_LSEEK_T avs_fs_lseek {};
        AVS_FS_READ_T avs_fs_read {};
        AVS_FS_OPENDIR_T avs_fs_opendir {};
        AVS_FS_READDIR_T avs_fs_readdir {};
        AVS_FS_CLOSEDIR_T avs_fs_closedir {};
        CSTREAM_CREATE_T cstream_create {};
        CSTREAM_OPERATE_T cstream_operate {};
        CSTREAM_FINISH_T cstream_finish {};
        CSTREAM_DESTROY_T cstream_destroy {};
        PROPERTY_NODE_READ_T property_node_read {};
        PROPERTY_NODE_WRITE_T property_node_write {};
        PROPERTY_FILE_WRITE_T property_file_write {};
        PROPERTY_NODE_TRAVERSAL_T property_node_traversal {};
        PROPERTY_PSMAP_EXPORT_T property_psmap_export {};
        PROPERTY_PSMAP_IMPORT_T property_psmap_import {};
        PROPERTY_NODE_NAME_T property_node_name {};
        PROPERTY_NODE_GET_DESC_T property_node_get_desc {};
        PROPERTY_GET_ERROR_T property_get_error {};
        PROPERTY_NODE_CLONE_T property_node_clone {};
        PROPERTY_QUERY_SIZE_T property_query_size {};
        PROPERTY_NODE_QUERY_STAT_T property_node_query_stat {};
        PROPERTY_NODE_DATASIZE_T property_node_datasize {};
        PROPERTY_MEM_WRITE_T property_mem_write {};
        PROPERTY_PART_WRITE_T property_part_write {};
        PROPERTY_NODE_ABSOLUTE_PATH_T property_node_absolute_path {};
        PROPERTY_NODE_HAS_T property_node_has {};
        PROPERTY_NODE_IS_ARRAY_T property_node_is_array {};
        PROPERTY_NODE_TYPE_T property_node_type {};
        PROPERTY_GET_ATTRIBUTE_BOOL_T property_get_attribute_bool {};
        PROPERTY_NODE_GET_ATTRIBUTE_BOOL_T property_node_get_attribute_bool {};
        PROPERTY_NODE_GET_ATTRIBUTE_U32_T property_node_get_attribute_u32 {};
        PROPERTY_NODE_GET_ATTRIBUTE_S32_T property_node_get_attribute_s32 {};
        PROPERTY_NODE_RENAME_T property_node_rename {};
        PROPERTY_QUERY_FREESIZE_T property_query_freesize {};
        PROPERTY_CLEAR_ERROR_T property_clear_error {};
        PROPERTY_LOOKUP_ENCODE_T property_lookup_encode {};
        PROPERTY_UNLOCK_FLAG_T property_unlock_flag {};
        PROPERTY_LOCK_FLAG_T property_lock_flag {};
        PROPERTY_SET_FLAG_T property_set_flag {};
        PROPERTY_PART_WRITE_META_T property_part_write_meta {};
        PROPERTY_PART_WRITE_META2_T property_part_write_meta2 {};
        PROPERTY_READ_DATA_T property_read_data {};
        PROPERTY_READ_META_T property_read_meta {};
        PROPERTY_GET_ATTRIBUTE_U32_T property_get_attribute_u32 {};
        PROPERTY_GET_ATTRIBUTE_S32_T property_get_attribute_s32 {};
        PROPERTY_GET_FINGERPRINT_T property_get_fingerprint {};
        PROPERTY_NODE_REFDATA_T property_node_refdata {};
        PROPERTY_INSERT_READ_WITH_FILENAME_T property_insert_read_with_filename {};
        PROPERTY_MEM_READ_T property_mem_read {};
        PROPERTY_READ_QUERY_MEMSIZE_LONG_T property_read_query_memsize_long {};
        PROPERTY_CLEAR_T property_clear {};
        AVS_NET_SOCKET_T avs_net_socket {};
        AVS_NET_GETSOCKOPT_T avs_net_getsockopt {};
        AVS_NET_SETSOCKOPT_T avs_net_setsockopt {};
        AVS_NET_CONNECT_T avs_net_connect {};
        AVS_NET_SEND_T avs_net_send {};
        AVS_NET_RECV_T avs_net_recv {};
        AVS_NET_POLL_T avs_net_poll {};
        AVS_NET_POLLFDS_ADD_T avs_net_pollfds_add {};
        AVS_NET_POLLFDS_GET_T avs_net_pollfds_get {};
        AVS_NET_ADD_PROTOCOL_T avs_net_add_protocol {};
        AVS_NET_ADD_PROTOCOL_LEGACY_T avs_net_add_protocol_legacy {};
        AVS_NET_DEL_PROTOCOL_T avs_net_del_protocol {};
        AVS_NET_ADDRINFOBYADDR_T avs_net_addrinfobyaddr {};
        AVS_NET_BIND_T avs_net_bind {};
        AVS_NET_CLOSE_T avs_net_close {};
        AVS_NET_SHUTDOWN_T avs_net_shutdown {};
        AVS_NET_GET_PEERNAME_T avs_net_get_peername {};
        AVS_NET_GET_SOCKNAME_T avs_net_get_sockname {};

        // state
        avs_core_import IMPORT_NAMES {};

        // settings
        Version VERSION = AVS21580;
        std::string VERSION_STR = "unknown";
        size_t HEAP_SIZE = 0x1000000;
        bool DEFAULT_HEAP_SIZE_SET = false;
        std::string LOG_PATH;
        std::string CFG_PATH;
        std::string LOG_LEVEL_CUSTOM;

        // handle
        HINSTANCE DLL_INSTANCE = nullptr;
        std::string DLL_NAME = "";

        // static fields
        static void *AVS_HEAP1 = nullptr;
        static void *AVS_HEAP2 = nullptr;

        // constants
        static constexpr struct avs_core_import IMPORT_LEGACY {
            .version                     = "legacy",
            .property_search             = "property_search",
            .boot                        = "avs_boot",
            .shutdown                    = "avs_shutdown",
            .property_desc_to_buffer     = "property_desc_to_buffer",
            .property_destroy            = "property_destroy",
            .property_read_query_memsize = "property_read_query_memsize",
            .property_create             = "property_create",
            .property_insert_read        = "property_insert_read",
            .property_node_create        = "property_node_create",
            .property_node_remove        = "property_node_remove",
            .property_node_refer         = "property_node_refer",
            .std_setenv                  = "std_setenv",

            .avs_fs_open                        = "avs_fs_open",
            .avs_fs_copy                        = "avs_fs_copy",
            .avs_fs_close                       = "avs_fs_close",
            .avs_fs_dump_mountpoint             = "avs_fs_dump_mountpoint",
            .avs_fs_mount                       = "avs_fs_mount",
            .avs_fs_fstat                       = "avs_fs_fstat",
            .avs_fs_lstat                       = "avs_fs_lstat",
            .avs_fs_lseek                       = "avs_fs_lseek",
            .avs_fs_read                        = "avs_fs_read",
            .avs_fs_opendir                     = "avs_fs_opendir",
            .avs_fs_readdir                     = "avs_fs_readdir",
            .avs_fs_closedir                    = "avs_fs_closedir",
            .cstream_create                     = "cstream_create",
            .cstream_operate                    = "cstream_operate",
            .cstream_finish                     = "cstream_finish",
            .cstream_destroy                    = "cstream_destroy",
            .property_node_read                 = "property_node_read",
            .property_node_write                = "property_node_write",
            .property_file_write                = "property_file_write",
            .property_node_traversal            = "property_node_traversal",
            .property_psmap_export              = "property_psmap_export",
            .property_psmap_import              = "property_psmap_import",
            .property_node_name                 = "property_node_name",
            .property_node_get_desc             = "property_node_get_desc",
            .property_get_error                 = "property_get_error",
            .property_node_clone                = "property_node_clone",
            .property_query_size                = "property_query_size",
            .property_node_query_stat           = "property_node_query_stat",
            .property_node_datasize             = "property_node_datasize",
            .property_mem_write                 = "property_mem_write",
            .property_part_write                = "property_part_write",
            .property_node_absolute_path        = "property_node_absolute_path",
            .property_node_has                  = "property_node_has",
            .property_node_is_array             = "property_node_is_array",
            .property_node_type                 = "property_node_type",
            .property_get_attribute_bool        = "property_get_attribute_bool",
            .property_node_get_attribute_bool   = "property_node_get_attribute_bool",
            .property_node_get_attribute_u32    = "property_node_get_attribute_u32",
            .property_node_get_attribute_s32    = "property_node_get_attribute_s32",
            .property_node_rename               = "property_node_rename",
            .property_query_freesize            = "property_query_freesize",
            .property_clear_error               = "property_clear_error",
            .property_lookup_encode             = "property_lookup_encode",
            .property_unlock_flag               = "property_unlock_flag",
            .property_lock_flag                 = "property_lock_flag",
            .property_set_flag                  = "property_set_flag",
            .property_part_write_meta           = "property_part_write_meta",
            .property_part_write_meta2          = "property_part_write_meta2",
            .property_read_data                 = "property_read_data",
            .property_read_meta                 = "property_read_meta",
            .property_get_attribute_u32         = "property_get_attribute_u32",
            .property_get_attribute_s32         = "property_get_attribute_s32",
            .property_get_fingerprint           = "property_get_fingerprint",
            .property_node_refdata              = "property_node_refdata",
            .property_insert_read_with_filename = "property_insert_read_with_filename",
            .property_mem_read                  = "property_mem_read",
            .property_read_query_memsize_long   = "property_read_query_memsize_long",
            .property_clear                     = "property_clear",
            .avs_net_add_protocol               = "avs_net_add_protocol",
            .avs_net_del_protocol               = "avs_net_del_protocol",
            .avs_net_addrinfobyaddr             = "avs_net_addrinfobyaddr",
            .avs_net_socket                     = "avs_net_socket",
            .avs_net_setsockopt                 = "avs_net_setsockopt",
            .avs_net_getsockopt                 = "avs_net_getsockopt",
            .avs_net_connect                    = "avs_net_connect",
            .avs_net_send                       = "avs_net_send",
            .avs_net_recv                       = "avs_net_recv",
            .avs_net_poll                       = "avs_net_poll",
            .avs_net_pollfds_add                = "avs_net_pollfds_add",
            .avs_net_pollfds_get                = "avs_net_pollfds_get",
            .avs_net_bind                       = "avs_net_bind",
            .avs_net_close                      = "avs_net_close",
            .avs_net_shutdown                   = "avs_net_shutdown",
            .avs_net_get_peername               = "avs_net_get_peername",
            .avs_net_get_sockname               = "avs_net_get_sockname",
        };
        static constexpr struct avs_core_import IMPORT_AVS21360 {
            .version                     = "2.13.6.0",
            .property_search             = "XC058ba50000fb",
            .boot                        = "XC058ba50000f4",
            .shutdown                    = "XC058ba5000154",
            .property_desc_to_buffer     = "XC058ba50000cd",
            .property_destroy            = "XC058ba500010f",
            .property_read_query_memsize = "XC058ba5000066",
            .property_create             = "XC058ba5000107",
            .property_insert_read        = "XC058ba5000016",
            .property_node_create        = "XC058ba5000143",
            .property_node_remove        = "XC058ba5000085",
            .property_node_refer         = "XC058ba5000113",
            .std_setenv                  = "XC058ba5000075",

            .avs_fs_open                        = "XC058ba50000b6",
            .avs_fs_copy                        = "XC058ba5000122",
            .avs_fs_close                       = "XC058ba500011b",
            .avs_fs_dump_mountpoint             = "XC058ba50000c8",
            .avs_fs_mount                       = "XC058ba500009c",
            .avs_fs_fstat                       = "XC058ba50000d0",
            .avs_fs_lstat                       = "XC058ba5000063",
            .avs_fs_lseek                       = "XC058ba500000f",
            .avs_fs_read                        = "XC058ba5000139",
            .avs_fs_opendir                     = "XC058ba50000dd",
            .avs_fs_readdir                     = "XC058ba5000086",
            .avs_fs_closedir                    = "XC058ba5000087",
            .cstream_create                     = "XC058ba5000118",
            .cstream_operate                    = "XC058ba5000078",
            .cstream_finish                     = "XC058ba5000130",
            .cstream_destroy                    = "XC058ba500012b",
            .property_node_read                 = "XC058ba5000001",
            .property_node_write                = "XC058ba5000146",
            .property_file_write                = "XC058ba500009b",
            .property_node_traversal            = "XC058ba500005f",
            .property_psmap_export              = "XC058ba5000071",
            .property_psmap_import              = "XC058ba5000068",
            .property_node_name                 = "XC058ba5000106",
            .property_node_get_desc             = "XC058ba5000042",
            .property_get_error                 = "XC058ba500012e",
            .property_node_clone                = "XC058ba5000103",
            .property_query_size                = "XC058ba5000101",
            .property_node_query_stat           = "XC058ba500015e",
            .property_node_datasize             = "XC058ba5000100",
            .property_mem_write                 = "XC058ba5000162",
            .property_part_write                = "XC058ba5000060",
            .property_node_absolute_path        = "XC058ba500013b",
            .property_node_has                  = "XC058ba5000074",
            .property_node_is_array             = "XC058ba5000116",
            .property_node_type                 = "XC058ba5000150",
            .property_get_attribute_bool        = "XC058ba5000024",
            .property_node_get_attribute_bool   = "XC058ba50000b4",
            .property_node_get_attribute_u32    = "XC058ba500000b",
            .property_node_get_attribute_s32    = "XC058ba500014b",
            .property_node_rename               = "XC058ba50000ee",
            .property_query_freesize            = "XC058ba500013a",
            .property_clear_error               = "XC058ba50000a7",
            .property_lookup_encode             = "XC058ba5000158",
            .property_unlock_flag               = "XC058ba5000127",
            .property_lock_flag                 = "XC058ba500011e",
            .property_set_flag                  = "XC058ba500014d",
            .property_part_write_meta           = "XC058ba50000d8",
            .property_part_write_meta2          = "XC058ba50000b5",
            .property_read_data                 = "property_read_data", // not found
            .property_read_meta                 = "property_read_meta", // not found
            .property_get_attribute_u32         = "XC058ba50000ab",
            .property_get_attribute_s32         = "XC058ba50000fe",
            .property_get_fingerprint           = "XC058ba50000ce",
            .property_node_refdata              = "XC058ba5000151",
            .property_insert_read_with_filename = "XC058ba500006c",
            .property_mem_read                  = "XC058ba5000132",
            .property_read_query_memsize_long   = "XC058ba5000091",
            .property_clear                     = "XC058ba5000128",
            .avs_net_add_protocol               = "XC058ba5000126",
            .avs_net_del_protocol               = "XC058ba5000028",
            .avs_net_addrinfobyaddr             = "XC058ba50000a8",
            .avs_net_socket                     = "XC058ba50000e3",
            .avs_net_setsockopt                 = "XC058ba5000009",
            .avs_net_getsockopt                 = "XC058ba50000a2",
            .avs_net_connect                    = "XC058ba5000000",
            .avs_net_send                       = "XC058ba50000ad",
            .avs_net_recv                       = "XC058ba5000163",
            .avs_net_poll                       = "XC058ba50000fa",
            .avs_net_pollfds_add                = "XC058ba5000077",
            .avs_net_pollfds_get                = "XC058ba500001e",
            .avs_net_bind                       = "XC058ba5000083",
            .avs_net_close                      = "XC058ba500010b",
            .avs_net_shutdown                   = "XC058ba500008d",
            .avs_net_get_peername               = "XC058ba5000031",
            .avs_net_get_sockname               = "XC058ba500005d",
        };
        static constexpr struct avs_core_import IMPORT_AVS21430 {
            .version                     = "2.14.3.0",
            .property_search             = "XC0bbe970000fa",
            .boot                        = "XC0bbe970000f2",
            .shutdown                    = "XC0bbe9700014d",
            .property_desc_to_buffer     = "XC0bbe970000c9",
            .property_destroy            = "XC0bbe9700010d",
            .property_read_query_memsize = "XC0bbe97000062",
            .property_create             = "XC0bbe97000105",
            .property_insert_read        = "XC0bbe97000017",
            .property_node_create        = "XC0bbe9700013f",
            .property_node_remove        = "XC0bbe97000080",
            .property_node_refer         = "XC0bbe97000112",
            .std_setenv                  = "XC0bbe97000070",

            .avs_fs_open                 = "XC0bbe970000b3",
            .avs_fs_copy                 = "XC0bbe97000120",
            .avs_fs_close                = "XC0bbe97000119",
            .avs_fs_dump_mountpoint      = "XC0bbe970000c5",
            .avs_fs_mount                = "XC0bbe97000099",
            .avs_fs_fstat                = "XC0bbe970000ce",
            .avs_fs_lstat                = "XC0bbe9700005f",
            .avs_fs_read                 = "XC0bbe97000134",
            .avs_fs_opendir              = "XC0bbe970000db",
            .property_file_write         = "property_file_write", // not found
            .property_get_error          = "property_get_error", // not found
            .avs_net_add_protocol        = "XC0bbe97000124",
            .avs_net_del_protocol        = "XC0bbe97000026",
            .avs_net_addrinfobyaddr      = "XC0bbe970000a4",
            .avs_net_socket              = "XC0bbe970000e0",
            .avs_net_setsockopt          = "XC0bbe9700000a",
            .avs_net_getsockopt          = "XC0bbe9700009e",
            .avs_net_connect             = "XC0bbe97000000",
            .avs_net_send                = "XC0bbe970000a9",
            .avs_net_recv                = "XC0bbe9700015d",
            .avs_net_poll                = "XC0bbe970000f9",
            .avs_net_pollfds_add         = "XC0bbe97000072",
            .avs_net_pollfds_get         = "XC0bbe9700001d",
            .avs_net_bind                = "XC0bbe9700007e",
            .avs_net_close               = "XC0bbe97000109",
            .avs_net_shutdown            = "XC0bbe9700008a",
            .avs_net_get_peername        = "XC0bbe9700002e",
            .avs_net_get_sockname        = "XC0bbe97000059",
        };
        static constexpr struct avs_core_import IMPORT_AVS21580 {
            .version                     = "2.15.8.0",
            .property_search             = "XCd229cc00012e",
            .boot                        = "XCd229cc0000aa",
            .shutdown                    = "XCd229cc00001d",
            .property_desc_to_buffer     = "XCd229cc0000fd",
            .property_destroy            = "XCd229cc00013c",
            .property_read_query_memsize = "XCd229cc0000ff",
            .property_create             = "XCd229cc000126",
            .property_insert_read        = "XCd229cc00009a",
            .property_node_create        = "XCd229cc00002c",
            .property_node_remove        = "XCd229cc000028",
            .property_node_refer         = "XCd229cc000009",
            .std_setenv                  = "XCd229cc000094",

            .avs_fs_open                        = "XCd229cc000090",
            .avs_fs_copy                        = "XCd229cc0000eb",
            .avs_fs_close                       = "XCd229cc00011f",
            .avs_fs_dump_mountpoint             = "XCd229cc0000e9",
            .avs_fs_mount                       = "XCd229cc0000ce",
            .avs_fs_fstat                       = "XCd229cc0000c3",
            .avs_fs_lstat                       = "XCd229cc0000c0",
            .avs_fs_lseek                       = "XCd229cc00004d",
            .avs_fs_read                        = "XCd229cc00010d",
            .avs_fs_opendir                     = "XCd229cc0000f0",
            .avs_fs_readdir                     = "XCd229cc0000bb",
            .avs_fs_closedir                    = "XCd229cc0000b8",
            .cstream_create                     = "XCd229cc000141",
            .cstream_operate                    = "XCd229cc00008c",
            .cstream_finish                     = "XCd229cc000025",
            .cstream_destroy                    = "XCd229cc0000e3",
            .property_node_read                 = "XCd229cc0000f3",
            .property_node_write                = "XCd229cc00002d",
            .property_file_write                = "XCd229cc000052",
            .property_node_traversal            = "XCd229cc000046",
            .property_psmap_export              = "XCd229cc000006",
            .property_psmap_import              = "XCd229cc000005",
            .property_node_name                 = "XCd229cc000049",
            .property_node_get_desc             = "XCd229cc000165",
            .property_get_error                 = "XCd229cc0000b5",
            .property_node_clone                = "XCd229cc00010a",
            .property_query_size                = "XCd229cc000032",
            .property_node_query_stat           = "XCd229cc0000b1",
            .property_node_datasize             = "XCd229cc000083",
            .property_mem_write                 = "XCd229cc000033",
            .property_part_write                = "XCd229cc000024",
            .property_node_absolute_path        = "XCd229cc00007c",
            .property_node_has                  = "XCd229cc00008a",
            .property_node_is_array             = "XCd229cc000142",
            .property_node_type                 = "XCd229cc000071",
            .property_get_attribute_bool        = "XCd229cc000043",
            .property_node_get_attribute_bool   = "XCd229cc000110",
            .property_node_get_attribute_u32    = "XCd229cc0000db",
            .property_node_get_attribute_s32    = "XCd229cc00011a",
            .property_node_rename               = "XCd229cc0000af",
            .property_query_freesize            = "XCd229cc000144",
            .property_clear_error               = "XCd229cc00014b",
            .property_lookup_encode             = "XCd229cc0000fc",
            .property_unlock_flag               = "XCd229cc000145",
            .property_lock_flag                 = "XCd229cc000121",
            .property_set_flag                  = "XCd229cc000035",
            .property_part_write_meta           = "XCd229cc00004f",
            .property_part_write_meta2          = "XCd229cc000107",
            .property_read_data                 = "XCd229cc0000de",
            .property_read_meta                 = "XCd229cc00010e",
            .property_get_attribute_u32         = "XCd229cc000148",
            .property_get_attribute_s32         = "XCd229cc00005f",
            .property_get_fingerprint           = "XCd229cc000057",
            .property_node_refdata              = "XCd229cc00009f",
            .property_insert_read_with_filename = "XCd229cc0000cd",
            .property_mem_read                  = "XCd229cc000039",
            .property_read_query_memsize_long   = "XCd229cc00002b",
            .property_clear                     = "XCd229cc0000c2",
            .avs_net_add_protocol               = "XCd229cc000156",
            .avs_net_del_protocol               = "XCd229cc00000f",
            .avs_net_addrinfobyaddr             = "XCd229cc000040",
            .avs_net_socket                     = "XCd229cc000026",
            .avs_net_setsockopt                 = "XCd229cc000092",
            .avs_net_getsockopt                 = "XCd229cc000084",
            .avs_net_connect                    = "XCd229cc000038",
            .avs_net_send                       = "XCd229cc00011d",
            .avs_net_recv                       = "XCd229cc000131",
            .avs_net_poll                       = "XCd229cc00011c",
            .avs_net_pollfds_add                = "XCd229cc00004b",
            .avs_net_pollfds_get                = "XCd229cc000105",
            .avs_net_bind                       = "XCd229cc00007e",
            .avs_net_close                      = "XCd229cc00009c",
            .avs_net_shutdown                   = "XCd229cc0000ac",
            .avs_net_get_peername               = "XCd229cc000085",
            .avs_net_get_sockname               = "XCd229cc0000b0",
        };
        static constexpr struct avs_core_import IMPORT_AVS21610 {
            .version                     = "2.16.1.0",
            .property_search             = "XCnbrep700008c",
            .boot                        = "XCnbrep700011a",
            .shutdown                    = "XCnbrep700011b",
            .property_desc_to_buffer     = "XCnbrep700007d",
            .property_destroy            = "XCnbrep700007c",
            .property_read_query_memsize = "XCnbrep700009b",
            .property_create             = "XCnbrep700007b",
            .property_insert_read        = "XCnbrep700007f",
            .property_node_create        = "XCnbrep700008d",
            .property_node_remove        = "XCnbrep700008e",
            .property_node_refer         = "XCnbrep700009a",
            .std_setenv                  = "XCnbrep70000cc",

            .avs_fs_open                        = "XCnbrep7000039",
            .avs_fs_copy                        = "XCnbrep7000050",
            .avs_fs_close                       = "XCnbrep7000040",
            .avs_fs_dump_mountpoint             = "XCnbrep7000053",
            .avs_fs_mount                       = "XCnbrep7000036",
            .avs_fs_fstat                       = "XCnbrep700004d",
            .avs_fs_lstat                       = "XCnbrep700004e",
            .avs_fs_lseek                       = "XCnbrep700003a",
            .avs_fs_read                        = "XCnbrep700003c",
            .avs_fs_opendir                     = "XCnbrep7000047",
            .avs_fs_readdir                     = "XCnbrep7000048",
            .avs_fs_closedir                    = "XCnbrep7000049",
            .cstream_create                     = "XCnbrep7000124",
            .cstream_operate                    = "XCnbrep7000126",
            .cstream_finish                     = "XCnbrep7000127",
            .cstream_destroy                    = "XCnbrep7000128",
            .property_node_read                 = "XCnbrep7000096",
            .property_node_write                = "XCnbrep7000097",
            .property_file_write                = "XCnbrep70000a1",
            .property_node_traversal            = "XCnbrep7000091",
            .property_psmap_export              = "XCnbrep700009c",
            .property_psmap_import              = "XCnbrep700009b",
            .property_node_name                 = "XCnbrep7000092",
            .property_node_get_desc             = "XCnbrep7000099",
            .property_get_error                 = "XCnbrep7000089",
            .property_node_clone                = "XCnbrep700008f",
            .property_query_size                = "XCnbrep700008a",
            .property_node_query_stat           = "XCnbrep70000b0",
            .property_node_datasize             = "XCnbrep7000095",
            .property_mem_write                 = "XCnbrep70000a3",
            .property_part_write                = "XCnbrep7000082",
            .property_node_absolute_path        = "XCnbrep70000b1",
            .property_node_has                  = "XCnbrep7000098",
            .property_node_is_array             = "XCnbrep7000094",
            .property_node_type                 = "XCnbrep7000093",
            .property_get_attribute_bool        = "XCnbrep70000ab",
            .property_node_get_attribute_bool   = "XCnbrep70000a8",
            .property_node_get_attribute_u32    = "XCnbrep70000a7",
            .property_node_get_attribute_s32    = "XCnbrep70000a6",
            .property_node_rename               = "XCnbrep70000ae",
            .property_query_freesize            = "XCnbrep700008b",
            .property_clear_error               = "XCnbrep7000088",
            .property_lookup_encode             = "XCnbrep70000a4",
            .property_unlock_flag               = "XCnbrep7000087",
            .property_lock_flag                 = "XCnbrep7000086",
            .property_set_flag                  = "XCnbrep7000085",
            .property_part_write_meta           = "XCnbrep7000084",
            .property_part_write_meta2          = "XCnbrep7000083",
            .property_read_data                 = "XCnbrep7000081",
            .property_read_meta                 = "XCnbrep7000080",
            .property_get_attribute_u32         = "XCnbrep70000aa",
            .property_get_attribute_s32         = "XCnbrep70000a9",
            .property_get_fingerprint           = "XCnbrep70000af",
            .property_node_refdata              = "XCnbrep7000090",
            .property_insert_read_with_filename = "XCnbrep70000a0",
            .property_mem_read                  = "XCnbrep70000a2",
            .property_read_query_memsize_long   = "XCnbrep700009b",
            .property_clear                     = "XCnbrep700007e",
            .avs_net_add_protocol               = "XCnbrep7000062",
            .avs_net_del_protocol               = "XCnbrep7000063",
            .avs_net_addrinfobyaddr             = "XCnbrep7000060",
            .avs_net_socket                     = "XCnbrep7000064",
            .avs_net_setsockopt                 = "XCnbrep7000065",
            .avs_net_getsockopt                 = "XCnbrep7000066",
            .avs_net_connect                    = "XCnbrep7000068",
            .avs_net_send                       = "XCnbrep700006d",
            .avs_net_recv                       = "XCnbrep7000071",
            .avs_net_poll                       = "XCnbrep7000075",
            .avs_net_pollfds_add                = "XCnbrep7000077",
            .avs_net_pollfds_get                = "XCnbrep7000078",
            .avs_net_bind                       = "XCnbrep7000067",
            .avs_net_close                      = "XCnbrep700006b",
            .avs_net_shutdown                   = "XCnbrep700006c",
            .avs_net_get_peername               = "XCnbrep7000079",
            .avs_net_get_sockname               = "XCnbrep700007a",
        };
        static constexpr struct avs_core_import IMPORT_AVS21630 {
            .version                     = "2.16.3.0",
            .property_search             = "XCnbrep70000a1",
            .boot                        = "XCnbrep7000129",
            .shutdown                    = "XCnbrep700012a",
            .property_desc_to_buffer     = "XCnbrep7000092",
            .property_destroy            = "XCnbrep7000091",
            .property_read_query_memsize = "XCnbrep70000b0",
            .property_create             = "XCnbrep7000090",
            .property_insert_read        = "XCnbrep7000094",
            .property_node_create        = "XCnbrep70000a2",
            .property_node_remove        = "XCnbrep70000a3",
            .property_node_refer         = "XCnbrep70000af",
            .std_setenv                  = "XCnbrep70000d4",

            .avs_fs_open                        = "XCnbrep700004e",
            .avs_fs_copy                        = "XCnbrep7000065",
            .avs_fs_close                       = "XCnbrep7000055",
            .avs_fs_dump_mountpoint             = "XCnbrep7000068",
            .avs_fs_mount                       = "XCnbrep700004b",
            .avs_fs_fstat                       = "XCnbrep7000062",
            .avs_fs_lstat                       = "XCnbrep7000063",
            .avs_fs_lseek                       = "XCnbrep700004f",
            .avs_fs_read                        = "XCnbrep7000051",
            .avs_fs_opendir                     = "XCnbrep700005c",
            .avs_fs_readdir                     = "XCnbrep700005d",
            .avs_fs_closedir                    = "XCnbrep700005e",
            .cstream_create                     = "XCnbrep7000130",
            .cstream_operate                    = "XCnbrep7000132",
            .cstream_finish                     = "XCnbrep7000133",
            .cstream_destroy                    = "XCnbrep7000134",
            .property_node_read                 = "XCnbrep70000ab",
            .property_node_write                = "XCnbrep70000ac",
            .property_file_write                = "XCnbrep70000b6",
            .property_node_traversal            = "XCnbrep70000a6",
            .property_psmap_export              = "XCnbrep70000b3",
            .property_psmap_import              = "XCnbrep70000b2",
            .property_node_name                 = "XCnbrep70000a7",
            .property_node_get_desc             = "XCnbrep70000ae",
            .property_get_error                 = "XCnbrep700009e",
            .property_node_clone                = "XCnbrep70000a4",
            .property_query_size                = "XCnbrep700009f",
            .property_node_query_stat           = "XCnbrep70000c5",
            .property_node_datasize             = "XCnbrep70000aa",
            .property_mem_write                 = "XCnbrep70000b8",
            .property_part_write                = "XCnbrep7000097",
            .property_node_absolute_path        = "XCnbrep70000c6",
            .property_node_has                  = "XCnbrep70000ad",
            .property_node_is_array             = "XCnbrep70000a9",
            .property_node_type                 = "XCnbrep70000a8",
            .property_get_attribute_bool        = "XCnbrep70000c0",
            .property_node_get_attribute_bool   = "XCnbrep70000bd",
            .property_node_get_attribute_u32    = "XCnbrep70000bc",
            .property_node_get_attribute_s32    = "XCnbrep70000bb",
            .property_node_rename               = "XCnbrep70000c3",
            .property_query_freesize            = "XCnbrep70000a0",
            .property_clear_error               = "XCnbrep700009d",
            .property_lookup_encode             = "XCnbrep70000b9",
            .property_unlock_flag               = "XCnbrep700009c",
            .property_lock_flag                 = "XCnbrep700009b",
            .property_set_flag                  = "XCnbrep700009a",
            .property_part_write_meta           = "XCnbrep7000099",
            .property_part_write_meta2          = "XCnbrep7000098",
            .property_read_data                 = "XCnbrep7000096",
            .property_read_meta                 = "XCnbrep7000095",
            .property_get_attribute_u32         = "XCnbrep70000bf",
            .property_get_attribute_s32         = "XCnbrep70000be",
            .property_get_fingerprint           = "XCnbrep70000c4",
            .property_node_refdata              = "XCnbrep70000a5",
            .property_insert_read_with_filename = "XCnbrep70000b5",
            .property_mem_read                  = "XCnbrep70000b7",
            .property_read_query_memsize_long   = "XCnbrep70000b1",
            .property_clear                     = "XCnbrep7000093",
            .avs_net_add_protocol               = "XCnbrep7000077",
            .avs_net_del_protocol               = "XCnbrep7000078",
            .avs_net_addrinfobyaddr             = "XCnbrep7000075",
            .avs_net_socket                     = "XCnbrep7000079",
            .avs_net_setsockopt                 = "XCnbrep700007a",
            .avs_net_getsockopt                 = "XCnbrep700007b",
            .avs_net_connect                    = "XCnbrep700007d",
            .avs_net_send                       = "XCnbrep7000082",
            .avs_net_recv                       = "XCnbrep7000086",
            .avs_net_poll                       = "XCnbrep700008a",
            .avs_net_pollfds_add                = "XCnbrep700008c",
            .avs_net_pollfds_get                = "XCnbrep700008d",
            .avs_net_bind                       = "XCnbrep700007c",
            .avs_net_close                      = "XCnbrep7000080",
            .avs_net_shutdown                   = "XCnbrep7000081",
            .avs_net_get_peername               = "XCnbrep700008f",
            .avs_net_get_sockname               = "XCnbrep700008e",
        };
        static constexpr struct avs_core_import IMPORT_AVS21651 {
            .version                     = "2.16.5.1",
            .property_search             = "XCnbrep70000a1",
            .boot                        = "XCnbrep7000129",
            .shutdown                    = "XCnbrep700012a",
            .property_desc_to_buffer     = "XCnbrep7000092",
            .property_destroy            = "XCnbrep7000091",
            .property_read_query_memsize = "XCnbrep70000b0",
            .property_create             = "XCnbrep7000090",
            .property_insert_read        = "XCnbrep7000094",
            .property_node_create        = "XCnbrep70000a2",
            .property_node_remove        = "XCnbrep70000a3",
            .property_node_refer         = "XCnbrep70000af",
            .std_setenv                  = "XCnbrep70000d4",

            .avs_fs_open                        = "XCnbrep700004e",
            .avs_fs_copy                        = "XCnbrep7000065",
            .avs_fs_close                       = "XCnbrep7000055",
            .avs_fs_dump_mountpoint             = "XCnbrep7000068",
            .avs_fs_mount                       = "XCnbrep700004b",
            .avs_fs_fstat                       = "XCnbrep7000062",
            .avs_fs_lstat                       = "XCnbrep7000063",
            .avs_fs_lseek                       = "XCnbrep700004f",
            .avs_fs_read                        = "XCnbrep7000051",
            .avs_fs_opendir                     = "XCnbrep700005c",
            .avs_fs_readdir                     = "XCnbrep700005d",
            .avs_fs_closedir                    = "XCnbrep700005e",
            .cstream_create                     = "XCnbrep7000130",
            .cstream_operate                    = "XCnbrep7000132",
            .cstream_finish                     = "XCnbrep7000133",
            .cstream_destroy                    = "XCnbrep7000134",
            .property_node_read                 = "XCnbrep70000ab",
            .property_node_write                = "XCnbrep70000ac",
            .property_file_write                = "XCnbrep70000b6",
            .property_node_traversal            = "XCnbrep70000a6",
            .property_psmap_export              = "XCnbrep70000b3",
            .property_psmap_import              = "XCnbrep70000b2",
            .property_node_name                 = "XCnbrep70000a7",
            .property_node_get_desc             = "XCnbrep70000ae",
            .property_get_error                 = "XCnbrep700009e",
            .property_node_clone                = "XCnbrep70000a4",
            .property_query_size                = "XCnbrep700009f",
            .property_node_query_stat           = "XCnbrep70000c5",
            .property_node_datasize             = "XCnbrep70000aa",
            .property_mem_write                 = "XCnbrep70000b8",
            .property_part_write                = "XCnbrep7000097",
            .property_node_absolute_path        = "XCnbrep70000c6",
            .property_node_has                  = "XCnbrep70000ad",
            .property_node_is_array             = "XCnbrep70000a9",
            .property_node_type                 = "XCnbrep70000a8",
            .property_get_attribute_bool        = "XCnbrep70000c0",
            .property_node_get_attribute_bool   = "XCnbrep70000bd",
            .property_node_get_attribute_u32    = "XCnbrep70000bc",
            .property_node_get_attribute_s32    = "XCnbrep70000bb",
            .property_node_rename               = "XCnbrep70000c3",
            .property_query_freesize            = "XCnbrep70000a0",
            .property_clear_error               = "XCnbrep700009d",
            .property_lookup_encode             = "XCnbrep70000b9",
            .property_unlock_flag               = "XCnbrep700009c",
            .property_lock_flag                 = "XCnbrep700009b",
            .property_set_flag                  = "XCnbrep700009a",
            .property_part_write_meta           = "XCnbrep7000099",
            .property_part_write_meta2          = "XCnbrep7000098",
            .property_read_data                 = "XCnbrep7000096",
            .property_read_meta                 = "XCnbrep7000095",
            .property_get_attribute_u32         = "XCnbrep70000bf",
            .property_get_attribute_s32         = "XCnbrep70000be",
            .property_get_fingerprint           = "XCnbrep70000c4",
            .property_node_refdata              = "XCnbrep70000a5",
            .property_insert_read_with_filename = "XCnbrep70000b5",
            .property_mem_read                  = "XCnbrep70000b7",
            .property_read_query_memsize_long   = "XCnbrep70000b1",
            .property_clear                     = "XCnbrep7000093",
            .avs_net_add_protocol               = "XCnbrep7000077",
            .avs_net_del_protocol               = "XCnbrep7000078",
            .avs_net_addrinfobyaddr             = "XCnbrep7000075",
            .avs_net_socket                     = "XCnbrep7000079",
            .avs_net_setsockopt                 = "XCnbrep700007a",
            .avs_net_getsockopt                 = "XCnbrep700007b",
            .avs_net_connect                    = "XCnbrep700007d",
            .avs_net_send                       = "XCnbrep7000082",
            .avs_net_recv                       = "XCnbrep7000086",
            .avs_net_poll                       = "XCnbrep700008a",
            .avs_net_pollfds_add                = "XCnbrep700008c",
            .avs_net_pollfds_get                = "XCnbrep700008d",
            .avs_net_bind                       = "XCnbrep700007c",
            .avs_net_close                      = "XCnbrep7000080",
            .avs_net_shutdown                   = "XCnbrep7000081",
            .avs_net_get_peername               = "XCnbrep700008f",
            .avs_net_get_sockname               = "XCnbrep700008e",
        };
        static constexpr struct avs_core_import IMPORT_AVS21671 {
            .version                     = "2.16.7.1",
            .property_search             = "XCnbrep70000a1",
            .boot                        = "XCnbrep7000129",
            .shutdown                    = "XCnbrep700012a",
            .property_desc_to_buffer     = "XCnbrep7000092",
            .property_destroy            = "XCnbrep7000091",
            .property_read_query_memsize = "XCnbrep70000b0",
            .property_create             = "XCnbrep7000090",
            .property_insert_read        = "XCnbrep7000094",
            .property_node_create        = "XCnbrep70000a2",
            .property_node_remove        = "XCnbrep70000a3",
            .property_node_refer         = "XCnbrep70000af",
            .std_setenv                  = "XCnbrep70000d4",

            .avs_fs_open                        = "XCnbrep700004e",
            .avs_fs_copy                        = "XCnbrep7000065",
            .avs_fs_close                       = "XCnbrep7000055",
            .avs_fs_dump_mountpoint             = "XCnbrep7000068",
            .avs_fs_mount                       = "XCnbrep700004b",
            .avs_fs_fstat                       = "XCnbrep7000062",
            .avs_fs_lstat                       = "XCnbrep7000063",
            .avs_fs_lseek                       = "XCnbrep700004f",
            .avs_fs_read                        = "XCnbrep7000051",
            .avs_fs_opendir                     = "XCnbrep700005c",
            .avs_fs_readdir                     = "XCnbrep700005d",
            .avs_fs_closedir                    = "XCnbrep700005e",
            .cstream_create                     = "XCnbrep7000130",
            .cstream_operate                    = "XCnbrep7000132",
            .cstream_finish                     = "XCnbrep7000133",
            .cstream_destroy                    = "XCnbrep7000134",
            .property_node_read                 = "XCnbrep70000ab",
            .property_node_write                = "XCnbrep70000ac",
            .property_file_write                = "XCnbrep70000b6",
            .property_node_traversal            = "XCnbrep70000a6",
            .property_psmap_export              = "XCnbrep70000b3",
            .property_psmap_import              = "XCnbrep70000b2",
            .property_node_name                 = "XCnbrep70000a7",
            .property_node_get_desc             = "XCnbrep70000ae",
            .property_get_error                 = "XCnbrep700009e",
            .property_node_clone                = "XCnbrep70000a4",
            .property_query_size                = "XCnbrep700009f",
            .property_node_query_stat           = "XCnbrep70000c5",
            .property_node_datasize             = "XCnbrep70000aa",
            .property_mem_write                 = "XCnbrep70000b8",
            .property_part_write                = "XCnbrep7000097",
            .property_node_absolute_path        = "XCnbrep70000c6",
            .property_node_has                  = "XCnbrep70000ad",
            .property_node_is_array             = "XCnbrep70000a9",
            .property_node_type                 = "XCnbrep70000a8",
            .property_get_attribute_bool        = "XCnbrep70000c0",
            .property_node_get_attribute_bool   = "XCnbrep70000bd",
            .property_node_get_attribute_u32    = "XCnbrep70000bc",
            .property_node_get_attribute_s32    = "XCnbrep70000bb",
            .property_node_rename               = "XCnbrep70000c3",
            .property_query_freesize            = "XCnbrep70000a0",
            .property_clear_error               = "XCnbrep700009d",
            .property_lookup_encode             = "XCnbrep70000b9",
            .property_unlock_flag               = "XCnbrep700009c",
            .property_lock_flag                 = "XCnbrep700009b",
            .property_set_flag                  = "XCnbrep700009a",
            .property_part_write_meta           = "XCnbrep7000099",
            .property_part_write_meta2          = "XCnbrep7000098",
            .property_read_data                 = "XCnbrep7000096",
            .property_read_meta                 = "XCnbrep7000095",
            .property_get_attribute_u32         = "XCnbrep70000bf",
            .property_get_attribute_s32         = "XCnbrep70000be",
            .property_get_fingerprint           = "XCnbrep70000c4",
            .property_node_refdata              = "XCnbrep70000a5",
            .property_insert_read_with_filename = "XCnbrep70000b5",
            .property_mem_read                  = "XCnbrep70000b7",
            .property_read_query_memsize_long   = "XCnbrep70000b1",
            .property_clear                     = "XCnbrep7000093",
            .avs_net_add_protocol               = "XCnbrep7000077",
            .avs_net_del_protocol               = "XCnbrep7000078",
            .avs_net_addrinfobyaddr             = "XCnbrep7000075",
            .avs_net_socket                     = "XCnbrep7000079",
            .avs_net_setsockopt                 = "XCnbrep700007a",
            .avs_net_getsockopt                 = "XCnbrep700007b",
            .avs_net_connect                    = "XCnbrep700007d",
            .avs_net_send                       = "XCnbrep7000082",
            .avs_net_recv                       = "XCnbrep7000086",
            .avs_net_poll                       = "XCnbrep700008a",
            .avs_net_pollfds_add                = "XCnbrep700008c",
            .avs_net_pollfds_get                = "XCnbrep700008d",
            .avs_net_bind                       = "XCnbrep700007c",
            .avs_net_close                      = "XCnbrep7000080",
            .avs_net_shutdown                   = "XCnbrep7000081",
            .avs_net_get_peername               = "XCnbrep700008f",
            .avs_net_get_sockname               = "XCnbrep700008e",
        };
        static constexpr struct avs_core_import IMPORT_AVS21681 {
            .version                     = "2.16.8.1",
            .property_search             = "XCnbrep70000a1",
            .boot                        = "XCnbrep7000129",
            .shutdown                    = "XCnbrep700012a",
            .property_desc_to_buffer     = "XCnbrep7000092",
            .property_destroy            = "XCnbrep7000091",
            .property_read_query_memsize = "XCnbrep70000b0",
            .property_create             = "XCnbrep7000090",
            .property_insert_read        = "XCnbrep7000094",
            .property_node_create        = "XCnbrep70000a2",
            .property_node_remove        = "XCnbrep70000a3",
            .property_node_refer         = "XCnbrep70000af",
            .std_setenv                  = "XCnbrep70000d4",

            .avs_fs_open                        = "XCnbrep700004e",
            .avs_fs_copy                        = "XCnbrep7000065",
            .avs_fs_close                       = "XCnbrep7000055",
            .avs_fs_dump_mountpoint             = "XCnbrep7000068",
            .avs_fs_mount                       = "XCnbrep700004b",
            .avs_fs_fstat                       = "XCnbrep7000062",
            .avs_fs_lstat                       = "XCnbrep7000063",
            .avs_fs_lseek                       = "XCnbrep700004f",
            .avs_fs_read                        = "XCnbrep7000051",
            .avs_fs_opendir                     = "XCnbrep700005c",
            .avs_fs_readdir                     = "XCnbrep700005d",
            .avs_fs_closedir                    = "XCnbrep700005e",
            .cstream_create                     = "XCnbrep7000130",
            .cstream_operate                    = "XCnbrep7000132",
            .cstream_finish                     = "XCnbrep7000133",
            .cstream_destroy                    = "XCnbrep7000134",
            .property_node_read                 = "XCnbrep70000ab",
            .property_node_write                = "XCnbrep70000ac",
            .property_file_write                = "XCnbrep70000b6",
            .property_node_traversal            = "XCnbrep70000a6",
            .property_psmap_export              = "XCnbrep70000b3",
            .property_psmap_import              = "XCnbrep70000b2",
            .property_node_name                 = "XCnbrep70000a7",
            .property_node_get_desc             = "XCnbrep70000ae",
            .property_get_error                 = "XCnbrep700009e",
            .property_node_clone                = "XCnbrep70000a4",
            .property_query_size                = "XCnbrep700009f",
            .property_node_query_stat           = "XCnbrep70000c5",
            .property_node_datasize             = "XCnbrep70000aa",
            .property_mem_write                 = "XCnbrep70000b8",
            .property_part_write                = "XCnbrep7000097",
            .property_node_absolute_path        = "XCnbrep70000c6",
            .property_node_has                  = "XCnbrep70000ad",
            .property_node_is_array             = "XCnbrep70000a9",
            .property_node_type                 = "XCnbrep70000a8",
            .property_get_attribute_bool        = "XCnbrep70000c0",
            .property_node_get_attribute_bool   = "XCnbrep70000bd",
            .property_node_get_attribute_u32    = "XCnbrep70000bc",
            .property_node_get_attribute_s32    = "XCnbrep70000bb",
            .property_node_rename               = "XCnbrep70000c3",
            .property_query_freesize            = "XCnbrep70000a0",
            .property_clear_error               = "XCnbrep700009d",
            .property_lookup_encode             = "XCnbrep70000b9",
            .property_unlock_flag               = "XCnbrep700009c",
            .property_lock_flag                 = "XCnbrep700009b",
            .property_set_flag                  = "XCnbrep700009a",
            .property_part_write_meta           = "XCnbrep7000099",
            .property_part_write_meta2          = "XCnbrep7000098",
            .property_read_data                 = "XCnbrep7000096",
            .property_read_meta                 = "XCnbrep7000095",
            .property_get_attribute_u32         = "XCnbrep70000bf",
            .property_get_attribute_s32         = "XCnbrep70000be",
            .property_get_fingerprint           = "XCnbrep70000c4",
            .property_node_refdata              = "XCnbrep70000a5",
            .property_insert_read_with_filename = "XCnbrep70000b5",
            .property_mem_read                  = "XCnbrep70000b7",
            .property_read_query_memsize_long   = "XCnbrep70000b1",
            .property_clear                     = "XCnbrep7000093",
            .avs_net_add_protocol               = "XCnbrep7000077",
            .avs_net_del_protocol               = "XCnbrep7000078",
            .avs_net_addrinfobyaddr             = "XCnbrep7000075",
            .avs_net_socket                     = "XCnbrep7000079",
            .avs_net_setsockopt                 = "XCnbrep700007a",
            .avs_net_getsockopt                 = "XCnbrep700007b",
            .avs_net_connect                    = "XCnbrep700007d",
            .avs_net_send                       = "XCnbrep7000082",
            .avs_net_recv                       = "XCnbrep7000086",
            .avs_net_poll                       = "XCnbrep700008a",
            .avs_net_pollfds_add                = "XCnbrep700008c",
            .avs_net_pollfds_get                = "XCnbrep700008d",
            .avs_net_bind                       = "XCnbrep700007c",
            .avs_net_close                      = "XCnbrep7000080",
            .avs_net_shutdown                   = "XCnbrep7000081",
            .avs_net_get_peername               = "XCnbrep700008f",
            .avs_net_get_sockname               = "XCnbrep700008e",
        };
        static constexpr struct avs_core_import IMPORT_AVS21700 {
            .version                     = "2.17.0.0",
            .property_search             = "XCgsqzn00000a1",
            .boot                        = "XCgsqzn0000129",
            .shutdown                    = "XCgsqzn000012a",
            .property_desc_to_buffer     = "XCgsqzn0000092",
            .property_destroy            = "XCgsqzn0000091",
            .property_read_query_memsize = "XCgsqzn00000b0",
            .property_create             = "XCgsqzn0000090",
            .property_insert_read        = "XCgsqzn0000094",
            .property_node_create        = "XCgsqzn00000a2",
            .property_node_remove        = "XCgsqzn00000a3",
            .property_node_refer         = "XCgsqzn00000af",
            .std_setenv                  = "XCgsqzn00000d4",

            .avs_fs_open                        = "XCgsqzn000004e",
            .avs_fs_copy                        = "XCgsqzn0000065",
            .avs_fs_close                       = "XCgsqzn0000055",
            .avs_fs_dump_mountpoint             = "XCgsqzn0000068",
            .avs_fs_mount                       = "XCgsqzn000004b",
            .avs_fs_fstat                       = "XCgsqzn0000062",
            .avs_fs_lstat                       = "XCgsqzn0000063",
            .avs_fs_lseek                       = "XCgsqzn000004f",
            .avs_fs_read                        = "XCgsqzn0000051",
            .avs_fs_opendir                     = "XCgsqzn000005c",
            .avs_fs_readdir                     = "XCgsqzn000005d",
            .avs_fs_closedir                    = "XCgsqzn000005e",
            .cstream_create                     = "XCgsqzn0000130",
            .cstream_operate                    = "XCgsqzn0000132",
            .cstream_finish                     = "XCgsqzn0000133",
            .cstream_destroy                    = "XCgsqzn0000134",
            .property_node_read                 = "XCgsqzn00000ab",
            .property_node_write                = "XCgsqzn00000ac",
            .property_file_write                = "XCgsqzn00000b6",
            .property_node_traversal            = "XCgsqzn00000a6",
            .property_psmap_export              = "XCgsqzn00000b3",
            .property_psmap_import              = "XCgsqzn00000b2",
            .property_node_name                 = "XCgsqzn00000a7",
            .property_node_get_desc             = "XCgsqzn00000ae",
            .property_get_error                 = "XCgsqzn000009e",
            .property_node_clone                = "XCgsqzn00000a4",
            .property_query_size                = "XCgsqzn000009f",
            .property_node_query_stat           = "XCgsqzn00000c5",
            .property_node_datasize             = "XCgsqzn00000aa",
            .property_mem_write                 = "XCgsqzn00000b8",
            .property_part_write                = "XCgsqzn0000097",
            .property_node_absolute_path        = "XCgsqzn00000c6",
            .property_node_has                  = "XCgsqzn00000ad",
            .property_node_is_array             = "XCgsqzn00000a9",
            .property_node_type                 = "XCgsqzn00000a8",
            .property_get_attribute_bool        = "XCgsqzn00000c0",
            .property_node_get_attribute_bool   = "XCgsqzn00000bd",
            .property_node_get_attribute_u32    = "XCgsqzn00000bc",
            .property_node_get_attribute_s32    = "XCgsqzn00000bb",
            .property_node_rename               = "XCgsqzn00000c3",
            .property_query_freesize            = "XCgsqzn00000a0",
            .property_clear_error               = "XCgsqzn000009d",
            .property_lookup_encode             = "XCgsqzn00000b9",
            .property_unlock_flag               = "XCgsqzn000009c",
            .property_lock_flag                 = "XCgsqzn000009b",
            .property_set_flag                  = "XCgsqzn000009a",
            .property_part_write_meta           = "XCgsqzn0000099",
            .property_part_write_meta2          = "XCgsqzn0000098",
            .property_read_data                 = "XCgsqzn0000096",
            .property_read_meta                 = "XCgsqzn0000095",
            .property_get_attribute_u32         = "XCgsqzn00000bf",
            .property_get_attribute_s32         = "XCgsqzn00000be",
            .property_get_fingerprint           = "XCgsqzn00000c4",
            .property_node_refdata              = "XCgsqzn00000a5",
            .property_insert_read_with_filename = "XCgsqzn00000b5",
            .property_mem_read                  = "XCgsqzn00000b7",
            .property_read_query_memsize_long   = "XCgsqzn00000b1",
            .property_clear                     = "XCgsqzn0000093",
            .avs_net_add_protocol               = "XCgsqzn0000077",
            .avs_net_del_protocol               = "XCgsqzn0000078",
            .avs_net_addrinfobyaddr             = "XCgsqzn0000075",
            .avs_net_socket                     = "XCgsqzn0000079",
            .avs_net_setsockopt                 = "XCgsqzn000007a",
            .avs_net_getsockopt                 = "XCgsqzn000007b",
            .avs_net_connect                    = "XCgsqzn000007d",
            .avs_net_send                       = "XCgsqzn0000082",
            .avs_net_recv                       = "XCgsqzn0000086",
            .avs_net_poll                       = "XCgsqzn000008a",
            .avs_net_pollfds_add                = "XCgsqzn000008c",
            .avs_net_pollfds_get                = "XCgsqzn000008d",
            .avs_net_bind                       = "XCgsqzn000007c",
            .avs_net_close                      = "XCgsqzn0000080",
            .avs_net_shutdown                   = "XCgsqzn0000081",
            .avs_net_get_peername               = "XCgsqzn000008f",
            .avs_net_get_sockname               = "XCgsqzn000008e",
        };
        static constexpr struct avs_core_import IMPORT_AVS21730 {
            .version                     = "2.17.3.0",
            .property_search             = "XCgsqzn00000a1",
            .boot                        = "XCgsqzn0000129",
            .shutdown                    = "XCgsqzn000012a",
            .property_desc_to_buffer     = "XCgsqzn0000092",
            .property_destroy            = "XCgsqzn0000091",
            .property_read_query_memsize = "XCgsqzn00000b0",
            .property_create             = "XCgsqzn0000090",
            .property_insert_read        = "XCgsqzn0000094",
            .property_node_create        = "XCgsqzn00000a2",
            .property_node_remove        = "XCgsqzn00000a3",
            .property_node_refer         = "XCgsqzn00000af",
            .std_setenv                  = "XCgsqzn00000d4",

            .avs_fs_open                        = "XCgsqzn000004e",
            .avs_fs_copy                        = "XCgsqzn0000065",
            .avs_fs_close                       = "XCgsqzn0000055",
            .avs_fs_dump_mountpoint             = "XCgsqzn0000068",
            .avs_fs_mount                       = "XCgsqzn000004b",
            .avs_fs_fstat                       = "XCgsqzn0000062",
            .avs_fs_lstat                       = "XCgsqzn0000063",
            .avs_fs_lseek                       = "XCgsqzn000004f",
            .avs_fs_read                        = "XCgsqzn0000051",
            .avs_fs_opendir                     = "XCgsqzn000005c",
            .avs_fs_readdir                     = "XCgsqzn000005d",
            .avs_fs_closedir                    = "XCgsqzn000005e",
            .cstream_create                     = "XCgsqzn0000130",
            .cstream_operate                    = "XCgsqzn0000132",
            .cstream_finish                     = "XCgsqzn0000133",
            .cstream_destroy                    = "XCgsqzn0000134",
            .property_node_read                 = "XCgsqzn00000ab",
            .property_node_write                = "XCgsqzn00000ac",
            .property_file_write                = "XCgsqzn00000b6",
            .property_node_traversal            = "XCgsqzn00000a6",
            .property_psmap_export              = "XCgsqzn00000b3",
            .property_psmap_import              = "XCgsqzn00000b2",
            .property_node_name                 = "XCgsqzn00000a7",
            .property_node_get_desc             = "XCgsqzn00000ae",
            .property_get_error                 = "XCgsqzn000009e",
            .property_node_clone                = "XCgsqzn00000a4",
            .property_query_size                = "XCgsqzn000009f",
            .property_node_query_stat           = "XCgsqzn00000c5",
            .property_node_datasize             = "XCgsqzn00000aa",
            .property_mem_write                 = "XCgsqzn00000b8",
            .property_part_write                = "XCgsqzn0000097",
            .property_node_absolute_path        = "XCgsqzn00000c6",
            .property_node_has                  = "XCgsqzn00000ad",
            .property_node_is_array             = "XCgsqzn00000a9",
            .property_node_type                 = "XCgsqzn00000a8",
            .property_get_attribute_bool        = "XCgsqzn00000c0",
            .property_node_get_attribute_bool   = "XCgsqzn00000bd",
            .property_node_get_attribute_u32    = "XCgsqzn00000bc",
            .property_node_get_attribute_s32    = "XCgsqzn00000bb",
            .property_node_rename               = "XCgsqzn00000c3",
            .property_query_freesize            = "XCgsqzn00000a0",
            .property_clear_error               = "XCgsqzn000009d",
            .property_lookup_encode             = "XCgsqzn00000b9",
            .property_unlock_flag               = "XCgsqzn000009c",
            .property_lock_flag                 = "XCgsqzn000009b",
            .property_set_flag                  = "XCgsqzn000009a",
            .property_part_write_meta           = "XCgsqzn0000099",
            .property_part_write_meta2          = "XCgsqzn0000098",
            .property_read_data                 = "XCgsqzn0000096",
            .property_read_meta                 = "XCgsqzn0000095",
            .property_get_attribute_u32         = "XCgsqzn00000bf",
            .property_get_attribute_s32         = "XCgsqzn00000be",
            .property_get_fingerprint           = "XCgsqzn00000c4",
            .property_node_refdata              = "XCgsqzn00000a5",
            .property_insert_read_with_filename = "XCgsqzn00000b5",
            .property_mem_read                  = "XCgsqzn00000b7",
            .property_read_query_memsize_long   = "XCgsqzn00000b1",
            .property_clear                     = "XCgsqzn0000093",
            .avs_net_add_protocol               = "XCgsqzn0000077",
            .avs_net_del_protocol               = "XCgsqzn0000078",
            .avs_net_addrinfobyaddr             = "XCgsqzn0000075",
            .avs_net_socket                     = "XCgsqzn0000079",
            .avs_net_setsockopt                 = "XCgsqzn000007a",
            .avs_net_getsockopt                 = "XCgsqzn000007b",
            .avs_net_connect                    = "XCgsqzn000007d",
            .avs_net_send                       = "XCgsqzn0000082",
            .avs_net_recv                       = "XCgsqzn0000086",
            .avs_net_poll                       = "XCgsqzn000008a",
            .avs_net_pollfds_add                = "XCgsqzn000008c",
            .avs_net_pollfds_get                = "XCgsqzn000008d",
            .avs_net_bind                       = "XCgsqzn000007c",
            .avs_net_close                      = "XCgsqzn0000080",
            .avs_net_shutdown                   = "XCgsqzn0000081",
            .avs_net_get_peername               = "XCgsqzn000008f",
            .avs_net_get_sockname               = "XCgsqzn000008e",
        };
        static const struct avs_core_import IMPORTS[AVS_VERSION_COUNT] = {
            IMPORT_LEGACY,
            IMPORT_AVS21360,
            IMPORT_AVS21430,
            IMPORT_AVS21580,
            IMPORT_AVS21610,
            IMPORT_AVS21630,
            IMPORT_AVS21651,
            IMPORT_AVS21671,
            IMPORT_AVS21681,
            IMPORT_AVS21700,
            IMPORT_AVS21730,
        };
        static const robin_hood::unordered_map<std::string, size_t> HEAP_SIZE_DEFAULTS = {
#ifdef SPICE64
            // beatmania IIDX 25
            {"bm2dx.dll", 0x8000000},

            // SOUND VOLTEX
            {"soundvoltex.dll", 0x10000000},
            // GITADORA
            {"gdxg.dll", 0x2000000},
#endif
            // jubeat
            {"jubeat.dll", 0x2000000},

            // MUSECA
            {"museca.dll", 0xC000000},

            // DDR ACE/A20
            {"arkmdxbio2.dll", 0x2000000},
            {"arkmdxp3.dll", 0x2000000},
            {"arkmdxp4.dll", 0x2000000},

            // Nostalgia
            {"nostalgia.dll", 0x8000000},

            // Bishi Bashi Channel
            {"bsch.dll", 0xC000000},

            // Quiz Magic Academy
            {"client.dll", 70000000},

            // Mahjong Fight Club
            {"system.dll", 0x2000000},

            // FutureTomTom
            {"arkmmd.dll", 0x22800000},

            // HELLO! Pop'n Music
            {"popn.dll", 0x1E00000},

            // Scotto
            {"scotto.dll", 160000000},

            // TsumTsum
            {"arko26.dll", 0x1E00000},

            // DANCERUSH
            {"superstep.dll", 0x18000000},

            // Winning Eleven 2012
            {"weac12_bootstrap_release.dll", 0x1E00000},

            // Winning Eleven 2014
            {"arknck.dll", 0x1E00000},

            // Otoca D'or
            {"arkkep.dll", 0x8000000},

            // Silent Scope: Bone Eater
            {"arkndd.dll", 0x1E00000},

            // Metal Gear Arcade
            {"launch.dll", 0x800000},

            // Ongaku Paradise
            {"arkjc9.dll", 0xA00000},

            // KAMUNITY 
            {"kamunity.dll", 33554432},
        };

        // apply a default heap size based on the game DLL name provided
        void set_default_heap_size(const std::string &dll_name) {

            // initialize the heap size with the default once
            if (DEFAULT_HEAP_SIZE_SET) {
                return;
            }

            if (HEAP_SIZE_DEFAULTS.find(dll_name) != HEAP_SIZE_DEFAULTS.end()) {
                auto old_size = HEAP_SIZE;

                HEAP_SIZE = HEAP_SIZE_DEFAULTS.at(dll_name);
                DEFAULT_HEAP_SIZE_SET = true;

                log_info("avs-core", "updated heap size: {} -> {}", old_size, avs::core::HEAP_SIZE);
            }
        }

        /*
         * Helpers
         */

        static BOOL output_callback(const char *buf, DWORD size, HANDLE file) {

            // check size
            if (size == 0) {
                return TRUE;
            }

            // state machine for parsing style and converting to CRLF
            // this is needed because newer AVS buffers multiple lines unlike the old callback
            static logger::Style last_style = logger::Style::DEFAULT;
            static logger::Style new_style = last_style;
            static size_t position = 0;
            for (size_t i = 0; i < size; i++) {
                switch (position) {
                    case 0: {
                        if (buf[i] == ']')
                            position++;
                        else
                            position = 0;
                        break;
                    }
                    case 1: {
                        if (buf[i] == ' ')
                            position++;
                        else
                            position = 0;
                        break;
                    }
                    case 2: {
                        switch (buf[i]) {
                            case 'M':
                                new_style = logger::Style::GREY;
                                break;
                            case 'I':
                                new_style = logger::Style::DEFAULT;
                                break;
                            case 'W':
                                new_style = logger::Style::YELLOW;
                                break;
                            case 'F':
                                new_style = logger::Style::RED;
                                deferredlogs::report_fatal_message();
                                break;
                            default:
                                position = 0;
                                break;
                        }
                        if (position > 0)
                            position++;
                        break;
                    }
                    case 3: {
                        position = 0;
                        if (buf[i] == ':') {
                            last_style = new_style;

                            // flush line
                            for (size_t j = i + 1; j < size; j++) {
                                if (buf[j] == '\n') {
                                    logger::push(std::string(buf, j), last_style, true);
                                    buf = &buf[j + 1];
                                    size -= j + 1;
                                    i = 0;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    default:
                        position = 0;
                        break;
                }
            }

            // push rest to logger
            if (size > 1) {
                logger::push(std::string(buf, size - 1), last_style, true);
            }

            // success
            return TRUE;
        }

        static BOOL output_callback_old(HANDLE file, const char *buf, DWORD size) {

            // check size
            if (size == 0) {
                return TRUE;
            }

            std::string out;

            // prefix
            if (buf[0] != '[') {
                out += log_get_datetime();
                out += " ";
            }

            // message
            out += std::string_view(buf, size - 1);

            // style
            logger::Style style = logger::Style::DEFAULT;
            switch (buf[0]) {
                case 'M':
                    style = logger::Style::GREY;
                    break;
                case 'F':
                    style = logger::Style::RED;
                    deferredlogs::report_fatal_message();
                    break;
                case 'W':
                    style = logger::Style::YELLOW;
                    break;
            }

            // push to logger
            logger::push(std::move(out), style);

            // success
            return TRUE;
        }

        static int avs_property_read_callback(uint32_t context, void *dst_buf, size_t count) {
            auto file = reinterpret_cast<FILE *>(TlsGetValue(context));
            return fread(dst_buf, 1, count, file);
        }

        property_ptr config_read(const std::string &filename, const size_t extra_space, const bool allow_fail) {

            // open file
            FILE *file = fopen(filename.c_str(), "rb");
            if (file == nullptr) {
                log_fatal("avs-core", "failed to open config file ({}): {}", filename, get_last_error_string());
            }

            // store handle in tls
            uint32_t tls = TlsAlloc();
            TlsSetValue(tls, file);

            // get file size
            int property_read_size = property_read_query_memsize(avs_property_read_callback, tls, 0, 0);
            if (property_read_size <= 0) {
                if (allow_fail) {
                    return nullptr;
                } else {
                    log_fatal("avs-core", "failed to read config file ({}): 0x{:x}",
                            filename,
                            static_cast<unsigned>(property_read_size));
                }
            }

            // add extra size (to be able to create new nodes, AVS does not resize properties
            // after initial allocation)
            property_read_size += extra_space;

            // allocate memory
            auto buffer = malloc(property_read_size);
            auto property = property_create(23, buffer, property_read_size);
            if (property == nullptr) {
                log_fatal("avs-core", "cannot create property: {}", filename);
            }

            // rewind file
            rewind(file);

            // read file contents
            if (property_insert_read(property, 0, avs_property_read_callback, tls) == 0) {
                log_fatal("avs-core", "cannot read property: {}", filename);
            }

            // clean up
            TlsFree(tls);
            fclose(file);

            // error checking
            if (avs::core::property_get_error) {
                auto err = avs::core::property_get_error(property);
                if (err > 0) {
                    log_fatal("avs-core", "failed to read config file ({}): {}", filename, error_str(err));
                }
            }

            // return value
            return property;
        }

        static int avs_property_read_string_callback(uint32_t context, void *dst_buf, size_t count) {
            auto input = reinterpret_cast<const char *>(TlsGetValue(context));
            memcpy(dst_buf, input, count);
            return count;
        }

        property_ptr config_read_string(const char *input) {

            // open tls store to hold pointer to input
            uint32_t tls = TlsAlloc();
            TlsSetValue(tls, const_cast<char *>(input));

            // get input size
            int property_read_size = property_read_query_memsize(
                    avs_property_read_string_callback, tls, 0, 0);

            // check file size
            if (property_read_size <= 0) {
                log_fatal("avs-core", "failed to reading config string: 0x{:x}",
                        static_cast<unsigned>(property_read_size));
            }

            // add extra size (to be able to create new nodes, AVS does not resize properties
            // after initial allocation)
            property_read_size += 1024;

            // allocate memory
            auto buffer = malloc(property_read_size);
            auto property = property_create(23, buffer, property_read_size);
            if (property == nullptr) {
                log_fatal("avs-core", "cannot create property");
            }

            // read string
            if (property_insert_read(property, 0, avs_property_read_string_callback, tls) == 0) {
                log_fatal("avs-core", "cannot read property");
            }

            // clean up
            TlsFree(tls);

            // return value
            return property;
        }

        node_ptr property_search_safe(property_ptr prop, node_ptr node, const char *name) {
            auto property = property_search(prop, node, name);
            if (property == nullptr) {
                log_fatal("avs-core", "node not found: {}", name);
            }
            return property;
        }

        void property_search_remove_safe(property_ptr prop, node_ptr node, const char *name) {
            auto prop_node = property_search(prop, node, name);
            if (prop_node == nullptr) {
                return;
            }
            property_node_remove(prop_node);
        }

        bool file_exists(const char *filename) {
            struct avs::core::avs_stat stat {};
            return avs_fs_lstat(filename, &stat) != 0;
        }

        void config_destroy(property_ptr prop) {
            void *mem = property_desc_to_buffer(prop);
            property_destroy(prop);
            free(mem);
        }

        /*
         * Functions
         */

        void create_log() {

            // set log path
            LOG_PATH = LOG_PATH.size() > 0 ? LOG_PATH : "log.txt";

            // create log file
            LOG_FILE = CreateFileA(
                    LOG_PATH.c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ,
                    nullptr,
                    CREATE_ALWAYS,
                    0,
                    nullptr
            );

            // remember actually used path
            if (LOG_FILE != INVALID_HANDLE_VALUE) {
                LOG_FILE_PATH = LOG_PATH;
            }
        }

        void load_dll() {
            log_info("avs-core", "loading DLL");

            // detect DLL name
            if (fileutils::file_exists(MODULE_PATH / "avs2-core.dll")) {
                DLL_NAME = "avs2-core.dll";
            } else {
#ifdef SPICE64
                DLL_NAME = "libavs-win64.dll";
#else
                DLL_NAME = "libavs-win32.dll";
#endif

                if (!fileutils::file_exists(MODULE_PATH / DLL_NAME)) {
                    std::string info_str { fmt::format(
                        "\n\n"
                        "Failed to find critical avs DLL on disk (avs2-core.dll OR {})\n"
                        "Looked in the following directory: {}\n"
                        "\n"
                        "One of these is required to boot the game. Spice found neither of them. You do not need both, just one, next to your game DLL.\n"
                        "\n"
                        "HOW TO FIX:\n"
                        "    * Avoid manually specifying DLL path (-exec) and module directory (-modules); let spice2x auto-detect unless you have a good reason not to\n"
                        "    * Ensure you do NOT have multiple copies of the game DLLs (e.g., in contents and in contents\\modules)\n"
                        "    * It's also possible that you have incomplete game data\n"
                        "    * Do NOT copy over random DLLs from another game installation; DLL must match game version\n"
                        "\n"
                    , DLL_NAME, MODULE_PATH.string()) };
                    log_warning("avs-ea3", "{}", info_str);
                    log_fatal("avs-ea3", "Failed to find critical avs DLL on disk (avs2-core.dll OR {})", DLL_NAME);
                }
            }

            // load library
            DLL_INSTANCE = libutils::load_library(MODULE_PATH / DLL_NAME);
            if (!DLL_INSTANCE) {
                return;
            }

            // check by version string if obtained
            char version[32] {};
            intptr_t ver = -1;
            if (fileutils::version_pe(MODULE_PATH / DLL_NAME, version)) {
                log_misc("avs-core", "version string: {}", version);

                for (size_t i = 0; i < AVS_VERSION_COUNT; i++) {
                    if (strcmp(IMPORTS[i].version, version) == 0) {
                        ver = i;
                        break;
                    }
                }
            }

            // check version by brute force
            if (ver < 0) {
                for (size_t i = 0; i < AVS_VERSION_COUNT; i++) {
                    if (GetProcAddress(DLL_INSTANCE, IMPORTS[i].property_search) != nullptr) {
                        ver = i;
                        break;
                    }
                }
            }

            // check if version was found
            if (ver < 0) {
                log_fatal("avs-core", "Unknown {}", DLL_NAME);
            }

            // print version
            VERSION = (avs::core::Version) ver;
            VERSION_STR = IMPORTS[ver].version;
            log_info("avs-core", "Found AVS2 core {}", IMPORTS[ver].version);

            // load functions
            avs::core::IMPORT_NAMES = IMPORTS[ver];
            avs215_boot = libutils::get_proc<AVS215_BOOT_T>(
                    DLL_INSTANCE, IMPORTS[ver].boot);
            avs216_boot = libutils::get_proc<AVS216_BOOT_T>(
                    DLL_INSTANCE, IMPORTS[ver].boot);
            avs_shutdown = libutils::get_proc<AVS_SHUTDOWN_T>(
                    DLL_INSTANCE, IMPORTS[ver].shutdown);
            property_node_create = libutils::get_proc<PROPERTY_NODE_CREATE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_create);
            property_desc_to_buffer = libutils::get_proc<PROPERTY_DESC_TO_BUFFER_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_desc_to_buffer);
            property_destroy = libutils::get_proc<PROPERTY_DESTROY_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_destroy);
            property_create = libutils::get_proc<PROPERTY_CREATE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_create);
            property_insert_read = libutils::get_proc<PROPERTY_INSERT_READ_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_insert_read);
            property_node_refer = libutils::get_proc<PROPERTY_NODE_REFER_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_refer);
            property_node_remove = libutils::get_proc<PROPERTY_NODE_REMOVE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_remove);
            property_read_query_memsize = libutils::get_proc<PROPERTY_READ_QUERY_MEMSIZE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_read_query_memsize);
            property_search = libutils::get_proc<PROPERTY_SEARCH_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_search);
            avs_std_setenv = libutils::get_proc<STD_SETENV_T>(
                    DLL_INSTANCE, IMPORTS[ver].std_setenv);
            avs_fs_open = libutils::try_proc<AVS_FS_OPEN_T>(
                DLL_INSTANCE, IMPORTS[ver].avs_fs_open);
            avs_fs_copy = libutils::try_proc<AVS_FS_COPY_T>(
                DLL_INSTANCE, IMPORTS[ver].avs_fs_copy);
            avs_fs_close = libutils::try_proc<AVS_FS_CLOSE_T>(
                DLL_INSTANCE, IMPORTS[ver].avs_fs_close);
            avs_fs_dump_mountpoint = libutils::try_proc<AVS_FS_DUMP_MOUNTPOINT_T>(
                DLL_INSTANCE, IMPORTS[ver].avs_fs_dump_mountpoint);
            avs_fs_mount = libutils::try_proc<AVS_FS_MOUNT_T>(
                DLL_INSTANCE, IMPORTS[ver].avs_fs_mount);
            avs_fs_lstat = libutils::try_proc<AVS_FS_LSTAT_T>(
                DLL_INSTANCE, IMPORTS[ver].avs_fs_lstat);

            // optional functions
            avs_fs_fstat = libutils::try_proc<AVS_FS_FSTAT_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_fs_fstat);
            avs_fs_lseek = libutils::try_proc<AVS_FS_LSEEK_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_fs_lseek);
            avs_fs_read = libutils::try_proc<AVS_FS_READ_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_fs_read);
            avs_fs_opendir = libutils::try_proc<AVS_FS_OPENDIR_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_fs_opendir);
            avs_fs_readdir = libutils::try_proc<AVS_FS_READDIR_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_fs_readdir);
            avs_fs_closedir = libutils::try_proc<AVS_FS_CLOSEDIR_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_fs_closedir);
            cstream_create = libutils::try_proc<CSTREAM_CREATE_T>(
                    DLL_INSTANCE, IMPORTS[ver].cstream_create);
            cstream_operate = libutils::try_proc<CSTREAM_OPERATE_T>(
                    DLL_INSTANCE, IMPORTS[ver].cstream_operate);
            cstream_finish = libutils::try_proc<CSTREAM_FINISH_T>(
                    DLL_INSTANCE, IMPORTS[ver].cstream_finish);
            cstream_destroy = libutils::try_proc<CSTREAM_DESTROY_T>(
                    DLL_INSTANCE, IMPORTS[ver].cstream_destroy);
            property_node_read = libutils::try_proc<PROPERTY_NODE_READ_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_read);
            property_node_write = libutils::try_proc<PROPERTY_NODE_WRITE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_write);
            property_file_write = libutils::try_proc<PROPERTY_FILE_WRITE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_file_write);
            property_node_traversal = libutils::try_proc<PROPERTY_NODE_TRAVERSAL_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_traversal);
            property_psmap_export = libutils::try_proc<PROPERTY_PSMAP_EXPORT_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_psmap_export);
            property_psmap_import = libutils::try_proc<PROPERTY_PSMAP_IMPORT_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_psmap_import);
            property_node_name = libutils::try_proc<PROPERTY_NODE_NAME_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_name);
            property_node_get_desc = libutils::try_proc<PROPERTY_NODE_GET_DESC_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_get_desc);
            property_get_error = libutils::try_proc<PROPERTY_GET_ERROR_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_get_error);
            property_node_clone = libutils::try_proc<PROPERTY_NODE_CLONE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_clone);
            property_query_size = libutils::try_proc<PROPERTY_QUERY_SIZE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_query_size);
            property_node_query_stat = libutils::try_proc<PROPERTY_NODE_QUERY_STAT_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_query_stat);
            property_node_datasize = libutils::try_proc<PROPERTY_NODE_DATASIZE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_datasize);
            property_mem_write = libutils::try_proc<PROPERTY_MEM_WRITE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_mem_write);
            property_part_write = libutils::try_proc<PROPERTY_PART_WRITE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_part_write);
            property_node_absolute_path = libutils::try_proc<PROPERTY_NODE_ABSOLUTE_PATH_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_absolute_path);
            property_node_has = libutils::try_proc<PROPERTY_NODE_HAS_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_has);
            property_node_is_array = libutils::try_proc<PROPERTY_NODE_IS_ARRAY_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_is_array);
            property_node_type = libutils::try_proc<PROPERTY_NODE_TYPE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_type);
            property_get_attribute_bool = libutils::try_proc<PROPERTY_GET_ATTRIBUTE_BOOL_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_get_attribute_bool);
            property_node_get_attribute_bool = libutils::try_proc<PROPERTY_NODE_GET_ATTRIBUTE_BOOL_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_get_attribute_bool);
            property_node_get_attribute_u32 = libutils::try_proc<PROPERTY_NODE_GET_ATTRIBUTE_U32_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_get_attribute_u32);
            property_node_get_attribute_s32 = libutils::try_proc<PROPERTY_NODE_GET_ATTRIBUTE_S32_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_get_attribute_s32);
            property_node_rename = libutils::try_proc<PROPERTY_NODE_RENAME_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_rename);
            property_query_freesize = libutils::try_proc<PROPERTY_QUERY_FREESIZE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_query_freesize);
            property_clear_error = libutils::try_proc<PROPERTY_CLEAR_ERROR_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_clear_error);
            property_lookup_encode = libutils::try_proc<PROPERTY_LOOKUP_ENCODE_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_lookup_encode);
            property_unlock_flag = libutils::try_proc<PROPERTY_UNLOCK_FLAG_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_unlock_flag);
            property_lock_flag = libutils::try_proc<PROPERTY_LOCK_FLAG_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_lock_flag);
            property_set_flag = libutils::try_proc<PROPERTY_SET_FLAG_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_set_flag);
            property_part_write_meta = libutils::try_proc<PROPERTY_PART_WRITE_META_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_part_write_meta);
            property_part_write_meta2 = libutils::try_proc<PROPERTY_PART_WRITE_META2_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_part_write_meta2);
            property_read_data = libutils::try_proc<PROPERTY_READ_DATA_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_read_data);
            property_read_meta = libutils::try_proc<PROPERTY_READ_META_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_read_meta);
            property_get_attribute_u32 = libutils::try_proc<PROPERTY_GET_ATTRIBUTE_U32_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_get_attribute_u32);
            property_get_attribute_s32 = libutils::try_proc<PROPERTY_GET_ATTRIBUTE_S32_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_get_attribute_s32);
            property_get_fingerprint = libutils::try_proc<PROPERTY_GET_FINGERPRINT_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_get_fingerprint);
            property_node_refdata = libutils::try_proc<PROPERTY_NODE_REFDATA_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_node_refdata);
            property_insert_read_with_filename = libutils::try_proc<PROPERTY_INSERT_READ_WITH_FILENAME_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_insert_read_with_filename);
            property_mem_read = libutils::try_proc<PROPERTY_MEM_READ_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_mem_read);
            property_read_query_memsize_long = libutils::try_proc<PROPERTY_READ_QUERY_MEMSIZE_LONG_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_read_query_memsize_long);
            property_clear = libutils::try_proc<PROPERTY_CLEAR_T>(
                    DLL_INSTANCE, IMPORTS[ver].property_clear);
            avs_net_socket = libutils::try_proc<AVS_NET_SOCKET_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_socket);
            avs_net_setsockopt = libutils::try_proc<AVS_NET_SETSOCKOPT_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_setsockopt);
            avs_net_getsockopt = libutils::try_proc<AVS_NET_GETSOCKOPT_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_getsockopt);
            avs_net_connect = libutils::try_proc<AVS_NET_CONNECT_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_connect);
            avs_net_send = libutils::try_proc<AVS_NET_SEND_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_send);
            avs_net_recv = libutils::try_proc<AVS_NET_RECV_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_recv);
            avs_net_poll = libutils::try_proc<AVS_NET_POLL_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_poll);
            avs_net_pollfds_add = libutils::try_proc<AVS_NET_POLLFDS_ADD_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_pollfds_add);
            avs_net_pollfds_get = libutils::try_proc<AVS_NET_POLLFDS_GET_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_pollfds_get);
            avs_net_add_protocol = libutils::try_proc<AVS_NET_ADD_PROTOCOL_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_add_protocol);
            avs_net_add_protocol_legacy = libutils::try_proc<AVS_NET_ADD_PROTOCOL_LEGACY_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_add_protocol);
            avs_net_del_protocol = libutils::try_proc<AVS_NET_DEL_PROTOCOL_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_del_protocol);
            avs_net_addrinfobyaddr = libutils::try_proc<AVS_NET_ADDRINFOBYADDR_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_addrinfobyaddr);
            avs_net_bind = libutils::try_proc<AVS_NET_BIND_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_bind);
            avs_net_close = libutils::try_proc<AVS_NET_CLOSE_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_close);
            avs_net_shutdown = libutils::try_proc<AVS_NET_SHUTDOWN_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_shutdown);
            avs_net_get_peername = libutils::try_proc<AVS_NET_GET_PEERNAME_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_get_peername);
            avs_net_get_sockname = libutils::try_proc<AVS_NET_GET_SOCKNAME_T>(
                    DLL_INSTANCE, IMPORTS[ver].avs_net_get_sockname);

            if (property_node_read) {
                log_misc("avs-core", "optional functions identified");
            }

            // success
            return;
        }

        static void avs_dir_err(const std::filesystem::path &src_path)
        {
            deferredlogs::defer_error_messages({
                "AVS filesystem initialization failure was previously detected during boot!",
                fmt::format("    ERROR: directory could not be created: {}", src_path.string().c_str()),
                fmt::format("    if you see a crash, it may have been caused by bad <mounttable> contents in {}", avs::core::CFG_PATH.c_str()),
                "    fix the XML file and try again"  
            });
        }

        static void create_avs_dir(
                const std::string_view &avs_path,
                const std::string_view &src_path)
        {
            std::error_code err;

            auto real_path = std::filesystem::absolute(src_path, err);

            if (err) {
                avs_dir_err(real_path);
                log_warning("avs-core", "failed to resolve '{}' path: {}", avs_path, err.message());
                return;
            }

            auto created = std::filesystem::create_directories(real_path, err);

            if (created) {
                log_info("avs-core", "created '{}' at '{}'", avs_path, real_path.string());
            }

            if (err) {
                avs_dir_err(real_path);
                log_warning("avs-core", "failed to create '{}' folder at '{}': {}",
                        avs_path,
                        real_path.string(),
                        err.message());
            }
        }

        static void create_avs_config_fs_dir(
                struct property_info *prop,
                struct node_info *node,
                const char *folder_name)
        {
            char device_path[2048] { 0 };
            char fs_type[255] { 0 };

            auto avs_path = fmt::format("dev/{}", folder_name);
            auto base_node_path = fmt::format("/fs/{}", folder_name);
            auto fs_node = property_search(prop, node, base_node_path.c_str());

            if (!fs_node) {
                return;
            }

            // check for `fstype` node, AVS defaults to the `nvram` fs if the `fstype` is unset
            auto fs_type_exists = property_search(prop, fs_node, "fstype") != nullptr;

            auto device_result = property_node_refer(prop, fs_node, "device", NODE_TYPE_str,
                        device_path, sizeof(device_path));
            auto fs_type_result = 0;

            if (device_result < 0) {
                log_warning("avs-core", "failed to get '{}/device' string value: 0x{:08x}",
                        base_node_path,
                        static_cast<unsigned>(device_result));
            }

            if (fs_type_exists) {
                fs_type_result = property_node_refer(prop, fs_node, "fstype", NODE_TYPE_str,
                        fs_type, sizeof(fs_type));

                if (fs_type_result < 0) {
                    log_warning("avs-core", "failed to get '{}/fstype' string value: 0x{:08x}",
                            base_node_path,
                            static_cast<unsigned>(fs_type_result));
                } else {

                    // only support 'fs' and 'nvram' fs types
                    if (_stricmp(fs_type, "fs") != 0 && _stricmp(fs_type, "nvram") != 0) {
                        log_misc("avs-core", "ignoring folder creation for '{}': unsupported fs type '{}'",
                                avs_path, fs_type);

                        return;
                    }
                }
            }

            if (device_result < 0 || fs_type_result < 0) {
                return;
            }

            create_avs_dir(avs_path, device_path);
        }

        static void create_avs_config_fs_table(
                struct property_info *prop,
                struct node_info *node)
        {
            char fs_type[255] { 0 };
            char src_path[2048] { 0 };
            char dst_path[2048] { 0 };

            auto table_node = property_search(prop, node, "/fs/mounttable");

            if (!table_node) {
                return;
            }

            auto vfs_node = property_search(prop, table_node, "vfs");

            if (!vfs_node) {
                return;
            }

            do {
                auto fs_type_result = property_node_refer(prop, vfs_node, "fstype@",
                        NODE_TYPE_attr, fs_type, sizeof(fs_type));
                auto src_result = property_node_refer(prop, vfs_node, "src@",
                        NODE_TYPE_attr, src_path, sizeof(src_path));
                auto dst_result = property_node_refer(prop, vfs_node, "dst@",
                        NODE_TYPE_attr, dst_path, sizeof(dst_path));

                if (fs_type_result < 0) {
                    log_warning("avs-core", "failed to get 'fs_type' string value: 0x{:08x}",
                            static_cast<unsigned>(fs_type_result));
                }
                if (src_result < 0) {
                    log_warning("avs-core", "failed to get 'src' string value: 0x{:08x}",
                            static_cast<unsigned>(src_result));
                }
                if (dst_result < 0) {
                    log_warning("avs-core", "failed to get 'dst' string value: 0x{:08x}",
                            static_cast<unsigned>(dst_result));
                }
                if (fs_type_result < 0 || src_result < 0 || dst_result < 0) {
                    continue;
                }

                // only support 'fs' and 'nvram' fs types
                if (_stricmp(fs_type, "fs") != 0 && _stricmp(fs_type, "nvram") != 0) {
                    log_misc("avs-core", "ignoring folder creation for '{}': unsupported fs type '{}'",
                            dst_path, fs_type);

                    continue;
                }

                create_avs_dir(dst_path, src_path);
            } while ((vfs_node = property_node_traversal(vfs_node, TRAVERSE_NEXT_SEARCH_RESULT)));
        }

        void boot() {
            // default config path
            CFG_PATH = !CFG_PATH.empty() ? CFG_PATH : "prop/avs-config.xml";

            log_info("avs-core", "booting (using {})", CFG_PATH);

            // read configuration
            auto config = config_read(CFG_PATH);
            auto config_node = property_search_safe(config, nullptr, "/config");

            // create nvram and raw directories if possible for non-mounttable configurations
            create_avs_config_fs_dir(config, config_node, "nvram");
            create_avs_config_fs_dir(config, config_node, "raw");

            // create nvram and raw directories if possible for mounttable configurations
            create_avs_config_fs_table(config, config_node);

            // set log level
            if (!LOG_LEVEL_CUSTOM.empty()) {
                property_search_remove_safe(config, config_node, "/log/level");

                if (VERSION == AVS21360 || VERSION == AVSLEGACY) {
                    uint32_t log_level = 0;

                    if (LOG_LEVEL_CUSTOM == "disable") {
                        log_level = 0;
                    } else if (LOG_LEVEL_CUSTOM == "fatal") {
                        log_level = 1;
                    } else if (LOG_LEVEL_CUSTOM == "warning") {
                        log_level = 2;
                    } else if (LOG_LEVEL_CUSTOM == "info") {
                        log_level = 3;
                    } else if (LOG_LEVEL_CUSTOM == "misc") {
                        log_level = 4;
                    } else if (LOG_LEVEL_CUSTOM == "all") {
                        log_level = 5;
                    }

                    property_node_create(config, config_node,
                            NODE_TYPE_u32, "/log/level", log_level);
                } else {
                    property_node_create(config, config_node,
                            NODE_TYPE_str, "/log/level", LOG_LEVEL_CUSTOM.c_str());
                }
            }

            // print log level
            static const char *LOG_LEVELS[] = { "disable", "fatal", "warning", "info", "misc", "all" };
            char current_log_level_buffer[32] { 0 };
            uint32_t current_log_level = 0;
            std::string log_level_as_str;

            if (VERSION == AVSLEGACY || VERSION == AVS21360) {
                auto level_node = property_search(config, config_node, "/log/level");

                // set the log level to `misc` if one is not present
                if (!level_node) {
                    level_node = property_node_create(config, config_node, NODE_TYPE_u32, "/log/level", 4);
                }

                // print the log level, if successfully retrieved
                if (property_node_refer(config, config_node, "/log/level",
                        NODE_TYPE_u32, &current_log_level, sizeof(current_log_level)) > 0)
                {
                    if (current_log_level < std::size(LOG_LEVELS)) {
                        log_info("avs-core", "log level: {}", LOG_LEVELS[current_log_level]);
                        log_level_as_str = LOG_LEVELS[current_log_level];
                    } else {
                        log_fatal("avs-core", "log level ({}) is invalid!", current_log_level);
                    }
                } else {
                    log_warning("avs-core", "log level: unknown");
                }
            } else {
                auto level_node = property_search(config, config_node, "/log/level");

                // convert old-style number log levels to new-style string log levels
                if (level_node && property_node_type && property_node_type(level_node) == NODE_TYPE_u32) {
                    property_node_refer(config, config_node, "/log/level",
                            NODE_TYPE_u32, &current_log_level, sizeof(current_log_level));

                    if (current_log_level < std::size(LOG_LEVELS)) {
                        property_node_remove(level_node);
                        level_node = property_node_create(config, config_node, NODE_TYPE_str, "/log/level",
                                LOG_LEVELS[current_log_level]);
                    } else {
                        log_fatal("avs-core", "log level ({}) is invalid! *", current_log_level);
                    }
                }

                // set the log level to `misc` if one is not present
                if (!level_node) {
                    level_node = property_node_create(config, config_node, NODE_TYPE_str, "/log/level", "misc");
                }

                // print the log level, if successfully retrieved
                if (property_node_refer(config, config_node, "/log/level",
                        NODE_TYPE_str, current_log_level_buffer, sizeof(current_log_level_buffer)) > 0)
                {
                    log_info("avs-core", "log level: {} *", current_log_level_buffer);
                    log_level_as_str = current_log_level_buffer;
                } else {
                    log_warning("avs-core", "log level: unknown *");
                }
            }

            if (log_level_as_str == "disable" ||
                log_level_as_str == "fatal" ||
                log_level_as_str == "warning" ||
                log_level_as_str == "info") {

                deferredlogs::defer_error_messages({
                    fmt::format(
                        "log level is set to `{}` (either in avs-config.xml or using -loglevel)", log_level_as_str),
                        "    this log file may have omitted important error messages from the game",
                        "    if you are troubleshooting crashes or failures, it is recommended that you",
                        "    set AVS Log Level (-loglevel) option to `all`",
                        });
            }

            // fix time offset
            auto t_now = std::time(nullptr);
            auto tm_now = *std::gmtime(&t_now);
            auto gmt =  mktime(&tm_now);
            property_search_remove_safe(config, config_node, "/time/gmt_offset");
            property_node_create(config, config_node,
                    NODE_TYPE_s32, "/time/gmt_offset", t_now - gmt);

            // use system time server
            property_search_remove_safe(config, config_node, "/sntp/ea_on");
            property_node_create(config, config_node,
                    NODE_TYPE_bool, "/sntp/ea_on", 0);

            // check heap size
            if (HEAP_SIZE <= 0) {
                log_fatal("avs-core", "invalid heap size (<= 0)");
            }
            log_misc("avs-core", "using heap size: {}", HEAP_SIZE);

            // initialize avs
            switch (VERSION) {
                case AVS21430:
                case AVS21580: {
                    AVS_HEAP1 = malloc(HEAP_SIZE);
                    if (!AVS_HEAP1) {
                        log_warning("avs-core", "could not allocate heap 1");
                    }
                    AVS_HEAP2 = malloc(HEAP_SIZE);
                    if (!AVS_HEAP2) {
                        log_warning("avs-core", "could not allocate heap 2");
                    }
                    avs215_boot(config_node, AVS_HEAP1, HEAP_SIZE, AVS_HEAP2, HEAP_SIZE,
                            (void *) &output_callback, LOG_FILE);
                    break;
                }
                case AVS21610:
                case AVS21630:
                case AVS21651:
                case AVS21671:
                case AVS21681:
                case AVS21700:
                case AVS21730: {
                    AVS_HEAP1 = malloc(HEAP_SIZE);
                    if (!AVS_HEAP1)
                        log_warning("avs-core", "could not allocate heap 1");
                    avs216_boot(config_node, AVS_HEAP1, HEAP_SIZE, nullptr,
                            (void *) &output_callback, LOG_FILE);
                    break;
                }
                case AVS21360:
                case AVSLEGACY: {
                    AVS_HEAP1 = malloc(HEAP_SIZE);
                    if (!AVS_HEAP1) {
                        log_warning("avs-core", "could not allocate heap 1");
                    }
                    AVS_HEAP2 = malloc(HEAP_SIZE);
                    if (!AVS_HEAP2) {
                        log_warning("avs-core", "could not allocate heap 2");
                    }
                    avs215_boot(config_node, AVS_HEAP1, HEAP_SIZE, AVS_HEAP2, HEAP_SIZE,
                            (void *) &output_callback_old, LOG_FILE);
                    break;
                }
                default:
                    log_fatal("avs-core", "unknown AVS boot procedure");
            }

            // wait a bit for output
            Sleep(100);

            // destroy config
            config_destroy(config);
        }

        void copy_defaults() {
            static robin_hood::unordered_map<std::string, std::optional<const char *>> NVRAM_DEFAULTS {
                { "ea3-config.xml", std::nullopt },
                { "eamuse-config.xml", "ea3-config.xml" },
                { "ea3-cfg.xml", "ea3-config.xml" },
                { "eacoin.xml", std::nullopt },
                { "coin.xml", std::nullopt },
                { "testmode-v.xml", std::nullopt },
            };

            for (auto &[prop_name, nvram_name] : NVRAM_DEFAULTS) {
                auto nvram = fmt::format("/dev/nvram/{}", nvram_name.has_value() ? nvram_name.value() : prop_name);

                // file not found in nvram
                if (!file_exists(nvram.c_str())) {
                    auto prop = fmt::format("/prop/defaults/{}", prop_name);

                    if (!file_exists(prop.c_str())) {
                        prop = fmt::format("/prop/{}", prop_name);

                        if (!file_exists(prop.c_str())) {
                            continue;
                        }
                    }

                    log_info("avs-core", "copying {} to {}", prop, nvram);
                    avs_fs_copy(prop.c_str(), nvram.c_str());
                }
            }
        }

        void shutdown() {
            log_info("avs-core", "shutdown");

            // call shutdown
            avs_shutdown();

            // clean heaps
            if (AVS_HEAP1) {
                free(AVS_HEAP1);
                AVS_HEAP1 = nullptr;
            }
            if (AVS_HEAP2) {
                free(AVS_HEAP2);
                AVS_HEAP2 = nullptr;
            }
        }

        const static struct {
            uint32_t error;
            const char* msg;
        } ERROR_LIST[] = {
                { 0x80092000, "invalid type" },
                { 0x80092001, "type cannot use as array" },
                { 0x80092002, "invalid" },
                { 0x80092003, "too large data size" },
                { 0x80092004, "too small buffer size" },
                { 0x80092005, "passcode 0 is not allowed" },
                { 0x80092040, "invalid node name" },
                { 0x80092041, "invalid attribute name" },
                { 0x80092042, "reserved attribute name" },
                { 0x80092043, "cannot find node/attribute" },
                { 0x80092080, "cannot allocate node" },
                { 0x80092081, "cannot allocate node value" },
                { 0x80092082, "cannot allocate mdigest for finger-print" },
                { 0x80092083, "cannot allocate nodename" },
                { 0x800920C0, "node type differs" },
                { 0x800920C1, "node type is VOID" },
                { 0x800920C2, "node is array" },
                { 0x800920C3, "node is not array" },
                { 0x80092100, "node is create-disabled" },
                { 0x80092101, "node is read-disabled" },
                { 0x80092102, "node is write-disabled" },
                { 0x80092103, "flag is already locked" },
                { 0x80092104, "passcode differs" },
                { 0x80092105, "insert_read() is applied to attribute" },
                { 0x80092106, "part_write() is applied to attribute" },
                { 0x80092107, "MODE_EXTEND flag differs" },
                { 0x80092140, "root node already exists" },
                { 0x80092141, "attribute cannot have children" },
                { 0x80092142, "node/attribute already exists" },
                { 0x80092143, "number of nodes exceeds 65535" },
                { 0x80092144, "cannot interpret as number" },
                { 0x80092145, "property is empty" },
                { 0x80092180, "I/O error" },
                { 0x80092181, "unexpected EOF" },
                { 0x80092182, "unknown format" },
                { 0x800921C0, "broken magic" },
                { 0x800921C1, "broken metadata" },
                { 0x800921C2, "broken databody" },
                { 0x800921C3, "invalid type" },
                { 0x800921C4, "too large data size" },
                { 0x800921C5, "too long node/attribute name" },
                { 0x800921C6, "attribute name is too long" },
                { 0x800921C7, "node/attribute already exists" },
                { 0x80092200, "invalid encoding" },
                { 0x80092201, "invalid XML token" },
                { 0x80092202, "XML syntax error" },
                { 0x80092203, "start tag / end tag mismatch" },
                { 0x80092204, "too large node data (__size mismatch)" },
                { 0x80092205, "too deep node tree" },
                { 0x80092206, "invalid type" },
                { 0x80092207, "invalid size" },
                { 0x80092208, "invalid count" },
                { 0x80092209, "invalid value" },
                { 0x8009220A, "invalid node name" },
                { 0x8009220B, "invalid attribute name" },
                { 0x8009220C, "reserved attribute name" },
                { 0x8009220D, "node/attribute already exists" },
                { 0x8009220E, "too many elements in node data" },
                { 0x80092240, "JSON syntax error" },
                { 0x80092241, "invalid JSON literal" },
                { 0x80092242, "invalid JSON number" },
                { 0x80092243, "invalid JSON string" },
                { 0x80092244, "invalid JSON object name" },
                { 0x80092245, "object name already exists" },
                { 0x80092246, "too long JSON object name" },
                { 0x80092247, "too deep JSON object/array nesting" },
                { 0x80092248, "cannot convert JSON array to property" },
                { 0x80092249, "cannot convert empty JSON object to property" },
                { 0x8009224A, "root node already exists" },
                { 0x8009224B, "cannot convert root node to TYPE_ARRAY" },
                { 0x8009224C, "name represents reserved attribute" },
                { 0x80092280, "finger-print differs" },
                { 0x800922C0, "operation is not supported" },
        };

        std::string error_str(int32_t error) {
            for (auto &e : ERROR_LIST) {
                if (e.error == (uint32_t) error) {
                    return e.msg;
                }
            }
            return fmt::format("unknown ({})", error);
        }
    }
}
