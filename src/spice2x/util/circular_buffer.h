#pragma once

#include <cstdio>
#include <memory>
#include <vector>

template<class T>
class circular_buffer {
public:

    explicit circular_buffer(size_t size) :
            buf_(std::unique_ptr<T[]>(new T[size])),
            size_(size) {
    }

    void put(T item) {
        buf_[head_] = item;
        head_ = (head_ + 1) % size_;

        if (head_ == tail_) {
            tail_ = (tail_ + 1) % size_;
        }
    }

    void put_all(const T *items, int size) {
        for (int i = 0; i < size; i++)
            this->put(items[i]);
    }

    void put_all(std::vector<T> items) {
        for (auto i : items)
            this->put(i);
    }

    T get() {
        if (empty()) {
            return T();
        }

        // read data and advance the tail (we now have a free space)
        auto val = buf_[tail_];
        tail_ = (tail_ + 1) % size_;

        return val;
    }

    std::vector<T> get_all() {
        std::vector<T> contents;
        contents.reserve(size());

        while (!empty()) {
            contents.push_back(get());
        }

        return contents;
    }

    T peek() {
        if (empty()) {
            return T();
        }

        // read data
        return buf_[tail_];
    }

    T *peek_ptr() {
        if (empty()) {
            return nullptr;
        }

        // read data
        return &buf_[tail_];
    }

    T peek(size_t pos) {
        if (empty()) {
            return T();
        }

        return buf_[(tail_ + pos) % size_];
    }

    T* peek_ptr(size_t pos) {
        if (empty())
            return nullptr;

        return &buf_[(tail_ + pos) % size_];
    }

    std::vector<T> peek_all() {
        const auto elements = size();
        std::vector<T> contents;
        contents.reserve(size());
        for (size_t i = 0; i < elements; i++)
            contents.push_back(peek(i));
        return contents;
    }

    void reset() {
        head_ = tail_;
    }

    bool empty() {

        // if head and tail are equal, we are empty
        return head_ == tail_;
    }

    bool full() {

        // if tail is ahead the head by 1, we are full
        return ((head_ + 1) % size_) == tail_;
    }

    size_t size() {
        if (tail_ > head_)
            return size_ + head_ - tail_;
        else
            return head_ - tail_;
    }

private:
    std::unique_ptr<T[]> buf_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_;
};
