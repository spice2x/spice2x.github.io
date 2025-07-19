#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <windows.h>
#include <sys/stat.h>

namespace avs {
    namespace core {

        /*
         * enums
         */
        enum node_type {
            NODE_TYPE_node = 1,
            NODE_TYPE_s8 = 2,
            NODE_TYPE_u8 = 3,
            NODE_TYPE_s16 = 4,
            NODE_TYPE_u16 = 5,
            NODE_TYPE_s32 = 6,
            NODE_TYPE_u32 = 7,
            NODE_TYPE_s64 = 8,
            NODE_TYPE_u64 = 9,
            NODE_TYPE_bin = 10,
            NODE_TYPE_str = 11,
            NODE_TYPE_ip4 = 12,
            NODE_TYPE_time = 13,
            NODE_TYPE_float = 14,
            NODE_TYPE_double = 15,
            NODE_TYPE_2s8 = 16,
            NODE_TYPE_2u8 = 17,
            NODE_TYPE_2s16 = 18,
            NODE_TYPE_2u16 = 19,
            NODE_TYPE_2s32 = 20,
            NODE_TYPE_2u32 = 21,
            NODE_TYPE_2s64 = 22,
            NODE_TYPE_2u64 = 23,
            NODE_TYPE_2f = 24,
            NODE_TYPE_2d = 25,
            NODE_TYPE_3s8 = 26,
            NODE_TYPE_3u8 = 27,
            NODE_TYPE_3s16 = 28,
            NODE_TYPE_3u16 = 29,
            NODE_TYPE_3s32 = 30,
            NODE_TYPE_3u32 = 31,
            NODE_TYPE_3s64 = 32,
            NODE_TYPE_3u64 = 33,
            NODE_TYPE_3f = 34,
            NODE_TYPE_3d = 35,
            NODE_TYPE_4s8 = 36,
            NODE_TYPE_4u8 = 37,
            NODE_TYPE_4s16 = 38,
            NODE_TYPE_4u16 = 39,
            NODE_TYPE_4s32 = 40,
            NODE_TYPE_4u32 = 41,
            NODE_TYPE_4s64 = 42,
            NODE_TYPE_4u64 = 43,
            NODE_TYPE_4f = 44,
            NODE_TYPE_4d = 45,
            NODE_TYPE_attr = 46,
            NODE_TYPE_attr_and_node = 47,
            NODE_TYPE_vs8 = 48,
            NODE_TYPE_vu8 = 49,
            NODE_TYPE_vs16 = 50,
            NODE_TYPE_vu16 = 51,
            NODE_TYPE_bool = 52,
            NODE_TYPE_2b = 53,
            NODE_TYPE_3b = 54,
            NODE_TYPE_4b = 55,
            NODE_TYPE_vb = 56,
        };

        enum psmap_type {
            PSMAP_TYPE_s8 = 2,
            PSMAP_TYPE_u8 = 3,
            PSMAP_TYPE_s16 = 4,
            PSMAP_TYPE_u16 = 5,
            PSMAP_TYPE_s32 = 6,
            PSMAP_TYPE_u32 = 7,
            PSMAP_TYPE_s64 = 8,
            PSMAP_TYPE_u64 = 9,
            PSMAP_TYPE_str = 10,
            PSMAP_TYPE_str2 = 11,
            PSMAP_TYPE_attr = 45,
            PSMAP_TYPE_bool = 50,
        };

        enum property_node_traversal_option {
            TRAVERSE_PARENT = 0,
            TRAVERSE_FIRST_CHILD = 1,
            TRAVERSE_FIRST_ATTR = 2,
            TRAVERSE_FIRST_SIBLING = 3,
            TRAVERSE_NEXT_SIBLING = 4,
            TRAVERSE_PREVIOUS_SIBLING = 5,
            TRAVERSE_LAST_SIBLING = 6,
            TRAVERSE_NEXT_SEARCH_RESULT = 7,
            TRAVERSE_PREV_SEARCH_RESULT = 8,
        };

        enum property_flag {
            PROP_XML                  = 0x000,
            PROP_READ                 = 0x001,
            PROP_WRITE                = 0x002,
            PROP_CREATE               = 0x004,
            PROP_BINARY               = 0x008,
            PROP_APPEND               = 0x010,
            PROP_DEBUG_VERBOSE        = 0x400,
            PROP_JSON                 = 0x800,
            PROP_BIN_PLAIN_NODE_NAMES = 0x1000,
        };

        enum cstream_type {
            CSTREAM_AVSLZ_DECOMPRESS = 0,
            CSTREAM_AVSLZ_COMPRESS = 1,
        };

        /*
         * structs
         */

        struct node_stat {
            int nodes;
            int data;
            int unk1, unk2, unk3;
        };
        typedef node_stat *node_stat_ptr;

        struct property_info {
            uint8_t dummy[560];
            uint32_t error_code;
            uint32_t has_error;
            uint32_t unk;
            int8_t buffer_offset;
        };
        typedef property_info *property_ptr;

        struct node_info {
            uint8_t dummy[47];
            node_type type;
        };
        typedef node_info *node_ptr;

        struct psmap_data {
            uint8_t type;
            uint8_t flags;
            uint16_t offset;
            uint32_t size;
            const char *path;
            void *xdefault;
        };
        typedef psmap_data *psmap_data_ptr;

        struct avs_stat {
            uint64_t st_atime;
            uint64_t st_mtime;
            uint64_t st_ctime;
            uint32_t unk1;
            uint32_t filesize;
            struct stat padding;
        };
        struct cstream_data {
            unsigned char *out_buf;
            unsigned char *in_buf;
            uint32_t out_size;
            uint32_t in_size;
        };
        struct avs_iovec {
            void *iov_base;
            size_t iov_len;
        };

        /*
         * net
         */

        constexpr int AVS_NET_PROTOCOL_MAGIC = 0x1b55fa1;
        constexpr int AVS_NET_POLL_POLLIN = 0x0001;
        constexpr int AVS_NET_PROTOCOL_SSL_TLS_V1_1 = 2;

        constexpr int T_NET_PROTO_ID_DEFAULT = 0;

        enum avs_net_sock_opts {
            AVS_SO_SNDTIMEO = 2,
            AVS_SO_RCVTIMEO = 3,
            AVS_SO_NONBLOCK = 9,
            AVS_SO_SSL_PROTOCOL = 10,
            AVS_SO_SSL_VERIFY_CN = 13,
        };

        typedef uint32_t avs_net_size_t;
        typedef uint16_t avs_net_port_t;
        typedef int avs_net_pollfds_size_t;

        typedef uint64_t avs_net_timeout_t;

        struct avs_net_poll_fd_opaque;
        struct avs_net_proto_desc_work;
        struct avs_net_sock_desc_work;

        struct avs_net_poll_fd {
            int socket;
            uint16_t events;
            uint16_t r_events;
            int error;
        };

        struct avs_net_protocol_ops {
            int (*protocol_initialize)(struct avs_net_proto_desc_work *work);
            int (*protocol_finalize)(struct avs_net_proto_desc_work *work);
            int (*allocate_socket)(struct avs_net_sock_desc_work *work);
            void (*free_socket)(struct avs_net_sock_desc_work *work);
            int (*initialize_socket)(struct avs_net_sock_desc_work *work);
            void (*finalize_socket)(struct avs_net_sock_desc_work *work);
            int (*setsockopt)(
                    struct avs_net_sock_desc_work *work,
                    unsigned int option_name,
                    const void *option_value,
                    avs_net_size_t option_len);
            int (*getsockopt)(
                    struct avs_net_sock_desc_work *work,
                    unsigned int option_name,
                    void *option_value,
                    avs_net_size_t *option_len);
            int (*bind)(
                    struct avs_net_sock_desc_work *work,
                    uint32_t address,
                    avs_net_port_t port);
            int (*connect)(
                    struct avs_net_sock_desc_work *work,
                    uint32_t address,
                    avs_net_port_t port);
            int (*listen)(struct avs_net_sock_desc_work *work, int backlog);
            int (*accept)(
                    struct avs_net_sock_desc_work *work,
                    void *new_sock,
                    uint32_t *address,
                    avs_net_port_t *port);
            int (*close)(struct avs_net_sock_desc_work *work);
            int (*shutdown)(struct avs_net_sock_desc_work *work, int how);
            int (*sendtov)(
                    struct avs_net_sock_desc_work *work,
                    const struct avs_iovec *iovec,
                    int iov_count,
                    uint32_t address,
                    avs_net_port_t port);
            int (*recvfromv)(
                    struct avs_net_sock_desc_work *work,
                    struct avs_iovec *iovec,
                    int iov_count,
                    uint32_t *address,
                    avs_net_port_t *port);
            int (*pollfds_add)(
                    struct avs_net_sock_desc_work *work,
                    struct avs_net_poll_fd_opaque *fds,
                    avs_net_pollfds_size_t fds_size,
                    struct avs_net_poll_fd *events);
            int (*pollfds_get)(
                    struct avs_net_sock_desc_work *work,
                    struct avs_net_poll_fd *events,
                    struct avs_net_poll_fd_opaque *fds);
            int (*sockpeer)(
                    struct avs_net_sock_desc_work *work,
                    bool peer_name,
                    uint32_t *address,
                    avs_net_port_t *port);
        };

        struct avs_net_protocol {
            struct avs_net_protocol_ops *ops;
            uint32_t magic;
            uint32_t protocol_id;
            uint32_t proto_work_size;
            uint32_t sock_work_size;
        };

        struct avs_net_protocol_legacy {
            struct avs_net_protocol_ops *ops;
            uint32_t protocol_id;
            uint32_t mystery;
            uint32_t sz_work;
        };

        /*
         * error
         */

        typedef int32_t avs_error_t;

        constexpr avs_error_t AVS_ERROR_MASK = 0x80000000;
        constexpr avs_error_t AVS_ERROR_FACILITY_MASK = 0x7fff;

        enum avs_error_class {
            AVS_ERROR_CLASS_NET = 8,
        };

        enum avs_error_subclass {
            AVS_ERROR_SUBCLASS_SC_INVAL    = 0x00000016,
            AVS_ERROR_SUBCLASS_SC_BADMSG   = 0x0000004a,
            AVS_ERROR_SUBCLASS_NET_TIMEOUT = 0x00001000,
        };

        inline avs_error_t avs_error_make(avs_error_class error_class, avs_error_subclass error_subclass) {
            return static_cast<avs_error_t>(
                    AVS_ERROR_MASK |
                    ((error_class & AVS_ERROR_FACILITY_MASK) << 16) |
                    (error_subclass & AVS_ERROR_FACILITY_MASK));
        }

        /*
         * misc
         */

        typedef int (*avs_reader_t)(uint32_t, void *, size_t);
        typedef uint32_t avs_file_t;

        // import mapping
        struct avs_core_import {

            // required functions
            const char *version;
            const char *property_search;
            const char *boot;
            const char *shutdown;
            const char *property_desc_to_buffer;
            const char *property_destroy;
            const char *property_read_query_memsize;
            const char *property_create;
            const char *property_insert_read;
            const char *property_node_create;
            const char *property_node_remove;
            const char *property_node_refer;
            const char *std_setenv;

            // optional functions
            const char *avs_fs_open = "?";
            const char *avs_fs_copy = "?";
            const char *avs_fs_close = "?";
            const char *avs_fs_dump_mountpoint = "?";
            const char *avs_fs_mount = "?";
            const char *avs_fs_fstat = "?";
            const char *avs_fs_lstat = "?";
            const char *avs_fs_lseek = "?";
            const char *avs_fs_read = "?";
            const char *avs_fs_opendir = "?";
            const char *avs_fs_readdir = "?";
            const char *avs_fs_closedir = "?";
            const char *cstream_create = "?";
            const char *cstream_operate = "?";
            const char *cstream_finish = "?";
            const char *cstream_destroy = "?";
            const char *property_node_read = "?";
            const char *property_node_write = "?";
            const char *property_file_write = "?";
            const char *property_node_traversal = "?";
            const char *property_psmap_export = "?";
            const char *property_psmap_import = "?";
            const char *property_node_name = "?";
            const char *property_node_get_desc = "?";
            const char *property_get_error = "?";
            const char *property_node_clone = "?";
            const char *property_query_size = "?";
            const char *property_node_query_stat = "?";
            const char *property_node_datasize = "?";
            const char *property_mem_write = "?";
            const char *property_part_write = "?";
            const char *property_node_absolute_path = "?";
            const char *property_node_has = "?";
            const char *property_node_is_array = "?";
            const char *property_node_type = "?";
            const char *property_get_attribute_bool = "?";
            const char *property_node_get_attribute_bool = "?";
            const char *property_node_get_attribute_u32 = "?";
            const char *property_node_get_attribute_s32 = "?";
            const char *property_node_rename = "?";
            const char *property_query_freesize = "?";
            const char *property_clear_error = "?";
            const char *property_lookup_encode = "?";
            const char *property_unlock_flag = "?";
            const char *property_lock_flag = "?";
            const char *property_set_flag = "?";
            const char *property_part_write_meta = "?";
            const char *property_part_write_meta2 = "?";
            const char *property_read_data = "?";
            const char *property_read_meta = "?";
            const char *property_get_attribute_u32 = "?";
            const char *property_get_attribute_s32 = "?";
            const char *property_get_fingerprint = "?";
            const char *property_node_refdata = "?";
            const char *property_insert_read_with_filename = "?";
            const char *property_mem_read = "?";
            const char *property_read_query_memsize_long = "?";
            const char *property_clear = "?";
            const char *avs_net_add_protocol = "?";
            const char *avs_net_del_protocol = "?";
            const char *avs_net_addrinfobyaddr = "?";
            const char *avs_net_socket = "?";
            const char *avs_net_setsockopt = "?";
            const char *avs_net_getsockopt = "?";
            const char *avs_net_connect = "?";
            const char *avs_net_send = "?";
            const char *avs_net_recv = "?";
            const char *avs_net_poll = "?";
            const char *avs_net_pollfds_add = "?";
            const char *avs_net_pollfds_get = "?";
            const char *avs_net_bind = "?";
            const char *avs_net_close = "?";
            const char *avs_net_shutdown = "?";
            const char *avs_net_get_peername = "?";
            const char *avs_net_get_sockname = "?";
        };
        extern avs_core_import IMPORT_NAMES;

        // settings
        enum Version {
            AVSLEGACY,
            AVS21360,
            AVS21430,
            AVS21580,
            AVS21610,
            AVS21630,
            AVS21651,
            AVS21671,
            AVS21681,
            AVS21700,
            AVS21730,
            AVS_VERSION_COUNT
        };
        extern Version VERSION;
        extern std::string VERSION_STR;
        extern size_t HEAP_SIZE;
        extern bool DEFAULT_HEAP_SIZE_SET;
        extern std::string LOG_PATH;
        extern std::string CFG_PATH;
        extern std::string LOG_LEVEL_CUSTOM;

        // handle
        extern HINSTANCE DLL_INSTANCE;
        extern std::string DLL_NAME;

        // helpers
        property_ptr config_read(const std::string &filename, size_t extra_space = 0, bool allow_fail = false);
        property_ptr config_read_string(const char* input);
        node_ptr property_search_safe(property_ptr prop, node_ptr node, const char *name);
        void property_search_remove_safe(property_ptr prop, node_ptr node, const char *name);
        bool file_exists(const char* filename);
        void config_destroy(property_ptr prop);
        std::string error_str(int32_t error);

        // functions
        void set_default_heap_size(const std::string &dll_name);
        void create_log();
        void load_dll();
        void boot();
        void copy_defaults();
        void shutdown();

        /*
         * library functions
         */

        typedef int (*AVS215_BOOT_T)(void *, void *, size_t, void *, size_t, void *, HANDLE);
        typedef int (*AVS216_BOOT_T)(void *, void *, size_t, void *, void *, HANDLE);
        extern AVS215_BOOT_T avs215_boot;
        extern AVS216_BOOT_T avs216_boot;

        typedef void (*AVS_SHUTDOWN_T)(void);
        extern AVS_SHUTDOWN_T avs_shutdown;

        typedef void *(*PROPERTY_DESC_TO_BUFFER_T)(property_ptr prop);
        extern PROPERTY_DESC_TO_BUFFER_T property_desc_to_buffer;

        typedef node_ptr (*PROPERTY_SEARCH_T)(property_ptr prop, node_ptr node, const char *path);
        extern PROPERTY_SEARCH_T property_search;

        typedef avs_error_t (*PROPERTY_DESTROY_T)(property_ptr prop);
        extern PROPERTY_DESTROY_T property_destroy;

        typedef int (*PROPERTY_READ_QUERY_MEMSIZE_T)(avs_reader_t reader, avs_file_t file, DWORD *, DWORD *);
        extern PROPERTY_READ_QUERY_MEMSIZE_T property_read_query_memsize;

        typedef property_ptr (*PROPERTY_CREATE_T)(int flags, void *buffer, uint32_t buffer_size);
        extern PROPERTY_CREATE_T property_create;

        typedef avs_error_t (*PROPERTY_INSERT_READ_T)(property_ptr prop, node_ptr node, avs_reader_t reader,
                                                    avs_file_t file);
        extern PROPERTY_INSERT_READ_T property_insert_read;

        typedef node_ptr (*PROPERTY_NODE_CREATE_T)(property_ptr prop, node_ptr node, node_type type, const char *path,
                                                   ...);
        extern PROPERTY_NODE_CREATE_T property_node_create;

        typedef avs_error_t (*PROPERTY_NODE_REMOVE_T)(node_ptr node);
        extern PROPERTY_NODE_REMOVE_T property_node_remove;

        typedef int (*PROPERTY_NODE_REFER_T)(property_ptr prop, node_ptr node, const char *path, node_type type,
                                             void *data, uint32_t data_size);
        extern PROPERTY_NODE_REFER_T property_node_refer;

        typedef int (*STD_SETENV_T)(const char *key, const char *value);
        extern STD_SETENV_T avs_std_setenv;

        /*
         * optional functions
         */

        typedef avs_file_t (*AVS_FS_OPEN_T)(const char *name, uint16_t mode, int flags);
        extern AVS_FS_OPEN_T avs_fs_open;

        typedef void (*AVS_FS_CLOSE_T)(avs_file_t file);
        extern AVS_FS_CLOSE_T avs_fs_close;

        typedef int (*AVS_FS_DUMP_MOUNTPOINT_T)(void);
        extern AVS_FS_DUMP_MOUNTPOINT_T avs_fs_dump_mountpoint;

        typedef int (*AVS_FS_MOUNT_T)(const char *mountpoint, const char *fsroot, const char *fstype, void *data);
        extern AVS_FS_MOUNT_T avs_fs_mount;

        typedef int (*AVS_FS_COPY_T)(const char *sname, const char *dname);
        extern AVS_FS_COPY_T avs_fs_copy;

        typedef int (*AVS_FS_FSTAT_T)(avs_file_t file, struct avs_stat *stat);
        extern AVS_FS_FSTAT_T avs_fs_fstat;

        typedef int (*AVS_FS_LSTAT_T)(const char *path, struct avs_stat *stat);
        extern AVS_FS_LSTAT_T avs_fs_lstat;

        typedef int (*AVS_FS_LSEEK_T)(avs_file_t file, long offset, int origin);
        extern AVS_FS_LSEEK_T avs_fs_lseek;

        typedef size_t (*AVS_FS_READ_T)(avs_file_t file, uint8_t *data, uint32_t data_size);
        extern AVS_FS_READ_T avs_fs_read;

        typedef avs_file_t (*AVS_FS_OPENDIR_T)(const char *path);
        extern AVS_FS_OPENDIR_T avs_fs_opendir;

        typedef const char* (*AVS_FS_READDIR_T)(avs_file_t dir);
        extern AVS_FS_READDIR_T avs_fs_readdir;

        typedef void (*AVS_FS_CLOSEDIR_T)(avs_file_t dir);
        extern AVS_FS_CLOSEDIR_T avs_fs_closedir;

        typedef struct cstream_data *(*CSTREAM_CREATE_T)(cstream_type type);
        extern CSTREAM_CREATE_T cstream_create;

        typedef bool (*CSTREAM_OPERATE_T)(struct cstream_data *data);
        extern CSTREAM_OPERATE_T cstream_operate;

        typedef bool (*CSTREAM_FINISH_T)(struct cstream_data *data);
        extern CSTREAM_FINISH_T cstream_finish;

        typedef bool (*CSTREAM_DESTROY_T)(struct cstream_data *data);
        extern CSTREAM_DESTROY_T cstream_destroy;

        typedef int (*PROPERTY_NODE_READ_T)(node_ptr node, node_type type, void *data, uint32_t data_size);
        extern PROPERTY_NODE_READ_T property_node_read;

        typedef uint32_t (*PROPERTY_NODE_WRITE_T)(node_ptr node, node_type type, void *data);
        extern PROPERTY_NODE_WRITE_T property_node_write;

        typedef int (*PROPERTY_FILE_WRITE_T)(property_ptr prop, const char *path);
        extern PROPERTY_FILE_WRITE_T property_file_write;

        typedef node_ptr (*PROPERTY_NODE_TRAVERSAL_T)(node_ptr node, enum property_node_traversal_option direction);
        extern PROPERTY_NODE_TRAVERSAL_T property_node_traversal;

        typedef bool (*PROPERTY_PSMAP_EXPORT_T)(property_ptr prop, node_ptr node, uint8_t *data, psmap_data_ptr psmap);
        extern PROPERTY_PSMAP_EXPORT_T property_psmap_export;

        typedef bool (*PROPERTY_PSMAP_IMPORT_T)(property_ptr prop, node_ptr node, uint8_t *data, psmap_data_ptr psmap);
        extern PROPERTY_PSMAP_IMPORT_T property_psmap_import;

        typedef avs_error_t (*PROPERTY_NODE_NAME_T)(node_ptr node, char *buffer, uint32_t buffer_size);
        extern PROPERTY_NODE_NAME_T property_node_name;

        typedef void *(*PROPERTY_NODE_GET_DESC_T)(node_ptr node);
        extern PROPERTY_NODE_GET_DESC_T property_node_get_desc;

        typedef uint32_t (*PROPERTY_GET_ERROR_T)(property_ptr prop);
        extern PROPERTY_GET_ERROR_T property_get_error;

        typedef bool (*PROPERTY_NODE_CLONE_T)(property_ptr dst_prop, node_ptr dst_node, node_ptr src_node, bool deep);
        extern PROPERTY_NODE_CLONE_T property_node_clone;

        typedef avs_error_t (*PROPERTY_QUERY_SIZE_T)(property_ptr prop);
        extern PROPERTY_QUERY_SIZE_T property_query_size;

        typedef avs_error_t (*PROPERTY_NODE_QUERY_STAT_T)(property_ptr prop, node_ptr node, node_stat_ptr stat);
        extern PROPERTY_NODE_QUERY_STAT_T property_node_query_stat;

        typedef int32_t (*PROPERTY_NODE_DATASIZE_T)(node_ptr node);
        extern PROPERTY_NODE_DATASIZE_T property_node_datasize;

        typedef int32_t (*PROPERTY_MEM_WRITE_T)(property_ptr prop, uint8_t *data, uint32_t data_size);
        extern PROPERTY_MEM_WRITE_T property_mem_write;

        typedef int32_t (*PROPERTY_PART_WRITE_T)(property_ptr prop, node_ptr node, uint8_t *data, uint32_t data_size);
        extern PROPERTY_PART_WRITE_T property_part_write;

        typedef avs_error_t (*PROPERTY_NODE_ABSOLUTE_PATH_T)(node_ptr node, char *buffer, uint32_t buffer_size,
                                                           bool attr);
        extern PROPERTY_NODE_ABSOLUTE_PATH_T property_node_absolute_path;

        typedef int32_t (*PROPERTY_NODE_HAS_T)(property_ptr prop, node_ptr node, int ukn);
        extern PROPERTY_NODE_HAS_T property_node_has;

        typedef bool (*PROPERTY_NODE_IS_ARRAY_T)(node_ptr node);
        extern PROPERTY_NODE_IS_ARRAY_T property_node_is_array;

        typedef node_type (*PROPERTY_NODE_TYPE_T)(node_ptr node);
        extern PROPERTY_NODE_TYPE_T property_node_type;

        typedef avs_error_t (*PROPERTY_GET_ATTRIBUTE_BOOL_T)(property_ptr prop, node_ptr node,
                const char *path, bool *value);
        extern PROPERTY_GET_ATTRIBUTE_BOOL_T property_get_attribute_bool;

        typedef avs_error_t (*PROPERTY_NODE_GET_ATTRIBUTE_BOOL_T)(node_ptr node, bool *value);
        extern PROPERTY_NODE_GET_ATTRIBUTE_BOOL_T property_node_get_attribute_bool;

        typedef avs_error_t (*PROPERTY_NODE_GET_ATTRIBUTE_U32_T)(node_ptr node, uint32_t *value);
        extern PROPERTY_NODE_GET_ATTRIBUTE_U32_T property_node_get_attribute_u32;

        typedef avs_error_t (*PROPERTY_NODE_GET_ATTRIBUTE_S32_T)(node_ptr node, int32_t *value);
        extern PROPERTY_NODE_GET_ATTRIBUTE_S32_T property_node_get_attribute_s32;

        typedef avs_error_t (*PROPERTY_NODE_RENAME_T)(node_ptr node, const char *name);
        extern PROPERTY_NODE_RENAME_T property_node_rename;

        typedef avs_error_t (*PROPERTY_QUERY_FREESIZE_T)(property_ptr prop, uint32_t freesize);
        extern PROPERTY_QUERY_FREESIZE_T property_query_freesize;

        typedef property_ptr (*PROPERTY_CLEAR_ERROR_T)(property_ptr prop);
        extern PROPERTY_CLEAR_ERROR_T property_clear_error;

        typedef uint32_t (*PROPERTY_LOOKUP_ENCODE_T)(property_ptr prop);
        extern PROPERTY_LOOKUP_ENCODE_T property_lookup_encode;

        typedef avs_error_t (*PROPERTY_UNLOCK_FLAG_T)(property_ptr prop, uint32_t flags);
        extern PROPERTY_UNLOCK_FLAG_T property_unlock_flag;

        typedef avs_error_t (*PROPERTY_LOCK_FLAG_T)(property_ptr prop, uint32_t flags);
        extern PROPERTY_LOCK_FLAG_T property_lock_flag;

        typedef uint32_t (*PROPERTY_SET_FLAG_T)(property_ptr prop, uint32_t set_flags, uint32_t clear_flags);
        extern PROPERTY_SET_FLAG_T property_set_flag;

        typedef int32_t (*PROPERTY_PART_WRITE_META_T)(property_ptr prop, node_ptr node, uint8_t buffer,
                uint32_t buffer_size, int ukn1, int ukn2);
        extern PROPERTY_PART_WRITE_META_T property_part_write_meta;

        typedef int32_t (*PROPERTY_PART_WRITE_META2_T)(property_ptr prop, node_ptr node, uint8_t buffer,
                uint32_t buffer_size);
        extern PROPERTY_PART_WRITE_META2_T property_part_write_meta2;

        typedef int32_t (*PROPERTY_READ_DATA_T)(property_ptr prop, node_ptr node, avs_reader_t read_func,
                uint32_t context);
        extern PROPERTY_READ_DATA_T property_read_data;

        typedef int32_t (*PROPERTY_READ_META_T)(property_ptr prop, node_ptr node, avs_reader_t read_func,
                uint32_t context);
        extern PROPERTY_READ_META_T property_read_meta;

        typedef avs_error_t (*PROPERTY_GET_ATTRIBUTE_U32_T)(property_ptr prop, node_ptr node, const char *path,
                uint32_t *value);
        extern PROPERTY_GET_ATTRIBUTE_U32_T property_get_attribute_u32;

        typedef avs_error_t (*PROPERTY_GET_ATTRIBUTE_S32_T)(property_ptr prop, node_ptr node, const char *path,
                int32_t *value);
        extern PROPERTY_GET_ATTRIBUTE_S32_T property_get_attribute_s32;

        typedef avs_error_t (*PROPERTY_GET_FINGERPRINT_T)(property_ptr prop, node_ptr node,
                uint8_t *data, uint32_t data_size);
        extern PROPERTY_GET_FINGERPRINT_T property_get_fingerprint;

        typedef uint8_t *(*PROPERTY_NODE_REFDATA_T)(node_ptr node);
        extern PROPERTY_NODE_REFDATA_T property_node_refdata;

        // TODO probably invalid signature
        typedef int32_t *(*PROPERTY_INSERT_READ_WITH_FILENAME_T)(property_ptr prop, node_ptr node, uint8_t *buffer,
                uint32_t buffer_size);
        extern PROPERTY_INSERT_READ_WITH_FILENAME_T property_insert_read_with_filename;

        // TODO probably invalid signature
        typedef int32_t *(*PROPERTY_MEM_READ_T)(property_ptr prop, node_ptr node, int flags, uint8_t *buffer,
                uint32_t buffer_size);
        extern PROPERTY_MEM_READ_T property_mem_read;

        typedef int (*PROPERTY_READ_QUERY_MEMSIZE_LONG_T)(avs_reader_t reader, avs_file_t file,
                DWORD *, DWORD *, DWORD *);
        extern PROPERTY_READ_QUERY_MEMSIZE_LONG_T property_read_query_memsize_long;

        typedef property_ptr (*PROPERTY_CLEAR_T)(property_ptr prop);
        extern PROPERTY_CLEAR_T property_clear;

        typedef avs_error_t (*AVS_NET_ADDRINFOBYADDR_T)(uint32_t addr, char* hostname, int hostname_size, int);
        extern AVS_NET_ADDRINFOBYADDR_T avs_net_addrinfobyaddr;

        typedef avs_error_t (*AVS_NET_ADD_PROTOCOL_T)(struct avs_net_protocol *protocol);
        extern AVS_NET_ADD_PROTOCOL_T avs_net_add_protocol;

        typedef avs_error_t (*AVS_NET_ADD_PROTOCOL_LEGACY_T)(struct avs_net_protocol_legacy *protocol);
        extern AVS_NET_ADD_PROTOCOL_LEGACY_T avs_net_add_protocol_legacy;

        typedef avs_error_t (*AVS_NET_DEL_PROTOCOL_T)(int protocol_id);
        extern AVS_NET_DEL_PROTOCOL_T avs_net_del_protocol;

        typedef int64_t (*AVS_NET_SOCKET_T)(int protocol_id);
        extern AVS_NET_SOCKET_T avs_net_socket;

        typedef avs_error_t (*AVS_NET_SETSOCKOPT_T)(
                int socket,
                int option_name,
                const void *option_value,
                avs_net_size_t option_len);
        extern AVS_NET_SETSOCKOPT_T avs_net_setsockopt;

        typedef avs_error_t (*AVS_NET_GETSOCKOPT_T)(
                int socket,
                unsigned int option_name,
                void *option_value,
                avs_net_size_t *option_len);
        extern AVS_NET_GETSOCKOPT_T avs_net_getsockopt;

        typedef avs_error_t (*AVS_NET_CONNECT_T)(int socket, uint32_t address, avs_net_port_t port);
        extern AVS_NET_CONNECT_T avs_net_connect;

        typedef avs_error_t (*AVS_NET_SEND_T)(int socket, uint8_t *buf, uint32_t buf_size);
        extern AVS_NET_SEND_T avs_net_send;

        typedef avs_error_t (*AVS_NET_RECV_T)(int socket, uint8_t *buf, uint32_t buf_size);
        extern AVS_NET_RECV_T avs_net_recv;

        typedef avs_error_t (*AVS_NET_POLL_T)(
                struct avs_net_poll_fd *fds,
                uint32_t num_fds,
                int timeout);
        extern AVS_NET_POLL_T avs_net_poll;

        typedef avs_error_t (*AVS_NET_POLLFDS_ADD_T)(
                int socket,
                struct avs_net_poll_fd_opaque *fds,
                avs_net_pollfds_size_t fds_size,
                struct avs_net_poll_fd *events);
        extern AVS_NET_POLLFDS_ADD_T avs_net_pollfds_add;

        typedef avs_error_t (*AVS_NET_POLLFDS_GET_T)(
                int socket,
                struct avs_net_poll_fd *events,
                struct avs_net_poll_fd_opaque *fds);
        extern AVS_NET_POLLFDS_GET_T avs_net_pollfds_get;

        typedef avs_error_t (*AVS_NET_BIND_T)(int socket, uint32_t address, avs_net_port_t port);
        extern AVS_NET_BIND_T avs_net_bind;

        typedef avs_error_t (*AVS_NET_CLOSE_T)(int socket);
        extern AVS_NET_CLOSE_T avs_net_close;

        typedef avs_error_t (*AVS_NET_SHUTDOWN_T)(int fd, int how);
        extern AVS_NET_SHUTDOWN_T avs_net_shutdown;

        typedef avs_error_t (*AVS_NET_GET_PEERNAME_T)(int fd, uint32_t *address, avs_net_port_t *port);
        extern AVS_NET_GET_PEERNAME_T avs_net_get_peername;

        typedef avs_error_t (*AVS_NET_GET_SOCKNAME_T)(int fd, uint32_t *address, avs_net_port_t *port);
        extern AVS_NET_GET_SOCKNAME_T avs_net_get_sockname;
    }
}
