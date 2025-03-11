#pragma once

namespace util {

    template<typename T>
    class IncompleteTypeDeleter {
    public:
        void operator()(T *wrapped_ptr) const {
            auto ptr = reinterpret_cast<uint8_t *>(wrapped_ptr);

            delete[] ptr;
        }
    };

    template<typename T>
    using unique_plain_ptr = std::unique_ptr<T, IncompleteTypeDeleter<T>>;

    template<typename T>
    inline std::unique_ptr<T, IncompleteTypeDeleter<T>> make_unique_plain(size_t size) {
        return unique_plain_ptr<T>(reinterpret_cast<T *>(new uint8_t[size]));
    }
}
