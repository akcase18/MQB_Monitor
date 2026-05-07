#pragma once

#include <stdint.h>

template<class T, uint8_t buf_size>
class RingBuffer
{
    public:
        RingBuffer() = default;

        /**
         * Add a new item to the ring buffer.
         *
         * @param item Item to add
         */
        void put(T item)
        {
            // Add new item to buffer
            _buf[_head] = item;

            // If the buffer is full
            if (_full)
            {
                // Overwrite oldest item in queue
                _tail = (_tail + 1) % _max_size;
            }

            // Advance the head
            _head = (_head + 1) % _max_size;

            // Check if it is full
            _full = _head == _tail;
        }

        /**
         * Get and remove the oldest item from the ring buffer.
         *
         * @return The oldest item in the ring buffer.
         */
        T get()
        {
            if (empty())
            {
                // Return default constructor for type
                // Probably should change this
                return T();
            }

            // Read oldest value
            auto val = _buf[_tail];
            // Can't be full, we just "emptied" 1 slot
            _full = false;
            // Advance the tail
            _tail = (_tail + 1) % _max_size;

            return val;
        }

        /**
         * Reset the ring buffer.
         */
        void reset()
        {
            _head = _tail;
            _full = false;
        }

        /**
         * Check if the ring buffer is empty.
         *
         * @return True if is it empty, otherwise false.
         */
        bool empty() const
        {
            return (!_full && (_head == _tail));
        }

        /**
         * Check if the ring buffer is full.
         *
         * @return True if it is full, otherwise false.
         */
        bool full() const
        {
            return _full;
        }

        /**
         * Get the capacity of the ring buffer.
         *
         * @return The maximum size of the ring buffer.
         */
        uint8_t capacity() const
        {
            return _max_size;
        }

        /**
         * Get the current size of the ring buffer, i.e. the number of items in
         * the ring buffer at that moment.
         *
         * @return The number of items in the ring buffer.
         */
        uint8_t size() const
        {
            // Start with max size
            uint8_t size = _max_size;

            // Make sure buffer is not full
            if (!_full)
            {
                if (_head >= _tail)
                {
                    size = _head - _tail;
                }
                else
                {
                    size = _max_size + _head - _tail;
                }
            }

            return size;
        }

    private:
        T       _buf[buf_size]; // Ring buffer
        uint8_t _head = 0;      // Index of "newest" item in buffer
        uint8_t _tail = 0;      // Index of "oldest" item in buffer
        uint8_t _max_size;      // Maximum size of buffer
        bool    _full = 0;
};