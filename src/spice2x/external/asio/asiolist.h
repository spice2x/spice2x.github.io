#pragma once

#include <vector>

#include <windows.h>

#define DRVERR                     -5000
#define DRVERR_INVALID_PARAM       DRVERR-1
#define DRVERR_DEVICE_ALREADY_OPEN DRVERR-2
#define DRVERR_DEVICE_NOT_FOUND    DRVERR-3

#define MAXPATHLEN 512
#define MAXDRVNAMELEN 128

struct AsioDriver {
    size_t id;
    char name[MAXDRVNAMELEN];
    char dll_path[MAXPATHLEN];
    CLSID clsid;
    void *instance;
    struct AsioDriver *next;
};

class AsioDriverList {
public:
    AsioDriverList();
    ~AsioDriverList();

    LONG open_driver(size_t driver_id, void **asio_driver);
    LONG close_driver(size_t driver_id);

    std::vector<AsioDriver> driver_list;

private:
    bool co_initialized = false;
};
