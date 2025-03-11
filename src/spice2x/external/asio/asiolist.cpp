#include "asiolist.h"

#include <windows.h>

#include "util/logging.h"

#include "iasiodrv.h"

#define ASIODRV_DESC "description"
#define INPROC_SERVER "InprocServer32"
#define ASIO_PATH "software\\asio"
#define COM_CLSID "clsid"

static LONG find_driver_path(char *clsid_str, char *dll_path, size_t dll_path_size) {
    HKEY hkEnum, hksub, hkpath;
    char data_buf[512];
    LONG cr, rc = -1;
    DWORD data_type, data_size;
    DWORD index;
    OFSTRUCT ofs;
    HFILE hfile;
    bool found = false;

    CharLowerBuffA(clsid_str, static_cast<DWORD>(strlen(clsid_str)));

    if ((cr = RegOpenKeyA(HKEY_CLASSES_ROOT,COM_CLSID,&hkEnum)) == ERROR_SUCCESS) {
        index = 0;

        while (cr == ERROR_SUCCESS && !found) {
            cr = RegEnumKeyA(hkEnum, index++, (LPTSTR)data_buf, 512);
            if (cr == ERROR_SUCCESS) {
                CharLowerBuffA(data_buf, static_cast<DWORD>(strlen(data_buf)));

                if (!(strcmp(data_buf, clsid_str))) {
                    if ((cr = RegOpenKeyExA(hkEnum, (LPCTSTR)data_buf, 0, KEY_READ, &hksub)) == ERROR_SUCCESS) {
                        if ((cr = RegOpenKeyExA(hksub,(LPCTSTR)INPROC_SERVER,0,KEY_READ,&hkpath)) == ERROR_SUCCESS) {
                            data_type = REG_SZ;
                            data_size = static_cast<DWORD>(dll_path_size);
                            cr = RegQueryValueExA(hkpath, nullptr, nullptr, &data_type, (LPBYTE)dll_path, &data_size);
                            if (cr == ERROR_SUCCESS) {
                                memset(&ofs,0,sizeof(OFSTRUCT));
                                ofs.cBytes = sizeof(OFSTRUCT);
                                hfile = OpenFile(dll_path, &ofs, OF_EXIST);
                                if (hfile) {
                                    rc = 0;
                                }
                            }
                            RegCloseKey(hkpath);
                        }
                        RegCloseKey(hksub);
                    }

                    // break out
                    found = true;
                }
            }
        }
        RegCloseKey(hkEnum);
    }

    return rc;
}

static bool new_driver_struct(HKEY hkey, char *key_name, size_t id, AsioDriver &driver) {
    HKEY hksub;
    char data_buffer[256];
    char dll_path[MAXPATHLEN];
    WORD wData[100];
    CLSID clsid;
    DWORD data_type, data_size;
    LONG cr, rc;

    if ((cr = RegOpenKeyExA(hkey, (LPCTSTR)key_name, 0, KEY_READ, &hksub)) == ERROR_SUCCESS) {
        data_type = REG_SZ;
        data_size = 256;
        cr = RegQueryValueExA(hksub, COM_CLSID, nullptr, &data_type, (LPBYTE)data_buffer, &data_size);
        if (cr == ERROR_SUCCESS) {
            rc = find_driver_path(data_buffer, dll_path, MAXPATHLEN);
            if (rc == 0) {
                driver.id = id;
                strcpy(driver.dll_path, dll_path);

                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data_buffer, -1, (LPWSTR)wData, 100);
                if ((cr = CLSIDFromString((LPOLESTR)wData,(LPCLSID)&clsid)) == S_OK) {
                    memcpy(&driver.clsid, &clsid, sizeof(CLSID));
                }

                data_type = REG_SZ;
                data_size = 256;
                cr = RegQueryValueExA(hksub, ASIODRV_DESC, nullptr, &data_type, (LPBYTE)data_buffer, &data_size);
                if (cr == ERROR_SUCCESS) {
                    strcpy(driver.name, data_buffer);
                } else {
                    strcpy(driver.name, key_name);
                }

                return true;
            }
        }
        RegCloseKey(hksub);
    }

    return false;
}

static void delete_driver_struct(AsioDriver &driver) {
    IAsio *iasio;

    if (driver.instance) {
        iasio = reinterpret_cast<IAsio *>(driver.instance);
        iasio->Release();
        driver.instance = nullptr;
    }
}

AsioDriverList::AsioDriverList() {
    HKEY hkEnum = nullptr;
    char key_name[MAXDRVNAMELEN];
    LONG cr;
    DWORD index = 0;

    cr = RegOpenKeyA(HKEY_LOCAL_MACHINE,ASIO_PATH, &hkEnum);
    while (cr == ERROR_SUCCESS) {
        if ((cr = RegEnumKeyA(hkEnum, index++, (LPTSTR) key_name, MAXDRVNAMELEN)) == ERROR_SUCCESS) {
            auto id = this->driver_list.size();
            if (!new_driver_struct(hkEnum, key_name, id, this->driver_list.emplace_back())) {
                this->driver_list.pop_back();
            }
        }
    }
    if (hkEnum) {
        RegCloseKey(hkEnum);
    }

    this->driver_list.shrink_to_fit();

    if (!this->driver_list.empty()) {

        // initialize COM
        HRESULT hr = CoInitialize(nullptr);
        if (FAILED(hr)) {
            log_warning("asio::driver_list", "CoInitialize failed, hr={}", FMT_HRESULT(hr));
        }
    }
}

AsioDriverList::~AsioDriverList() {
    if (!this->driver_list.empty()) {
        for (auto &driver : this->driver_list) {
            delete_driver_struct(driver);
        }
    }

    if (this->co_initialized) {
        CoUninitialize();
    }
}

LONG AsioDriverList::open_driver(size_t driver_id, void **asio_driver) {
    long rc;

    if (!asio_driver) {
        return DRVERR_INVALID_PARAM;
    }

    if (driver_id < this->driver_list.size()) {
        auto &driver = this->driver_list[driver_id];

        if (!driver.instance) {
            rc = CoCreateInstance(driver.clsid, nullptr, CLSCTX_INPROC_SERVER, driver.clsid, asio_driver);
            if (rc == S_OK) {
                driver.instance = *asio_driver;
                return 0;
            }
            // else if (rc == REGDB_E_CLASSNOTREG)
            //  strcpy (info->messageText, "Driver not registered in the Registration Database!");
        } else {
            rc = DRVERR_DEVICE_ALREADY_OPEN;
        }
    } else {
        rc = DRVERR_DEVICE_NOT_FOUND;
    }

    return rc;
}

LONG AsioDriverList::close_driver(size_t driver_id) {
    IAsio *iasio;

    if (driver_id < this->driver_list.size()) {
        auto &driver = this->driver_list[driver_id];

        if (driver.instance) {
            iasio = reinterpret_cast<IAsio *>(driver.instance);
            iasio->Release();
            driver.instance = nullptr;
        }
    }

    return 0;
}
