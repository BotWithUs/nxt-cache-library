#pragma once

#include <cstdint>
#include <cstring>
#include <string>

class RSBuffer
{
public:
    char *buffer;
    unsigned int writePosition{};
    unsigned int readPosition{};
    size_t capacity;

    bool ownsBuffer;

    explicit RSBuffer(char *buffer, size_t size)
            : buffer(buffer), writePosition(static_cast<unsigned int>(size)),
              readPosition(0), capacity(size), ownsBuffer(false)
    {
    }

    explicit RSBuffer(size_t size)
            : buffer(nullptr), writePosition(0), readPosition(0),
              capacity(size), ownsBuffer(true)
    {
        buffer = new char[size];
    }

    RSBuffer(const RSBuffer &other)
            : buffer(nullptr),
              writePosition(other.writePosition),
              readPosition(other.readPosition),
              capacity(other.capacity),
              ownsBuffer(false)
    {
        if (capacity > 0)
        {
            buffer = new char[capacity];
            std::memcpy(buffer, other.buffer, capacity);
            ownsBuffer = true;
        }
    }

    RSBuffer &operator=(const RSBuffer &other)
    {
        if (this == &other)
        {
            return *this;
        }

        char *newBuffer = nullptr;
        if (other.capacity > 0)
        {
            newBuffer = new char[other.capacity];
            std::memcpy(newBuffer, other.buffer, other.capacity);
        }

        if (ownsBuffer && buffer != nullptr)
        {
            delete[] buffer;
        }

        buffer = newBuffer;
        capacity = other.capacity;
        writePosition = other.writePosition;
        readPosition = other.readPosition;
        ownsBuffer = (capacity > 0);

        return *this;
    }

    RSBuffer(char *buffer, size_t capacity, unsigned int writePos, unsigned int readPos, bool ownsBuffer)
            : buffer(buffer), writePosition(writePos), readPosition(readPos),
              capacity(capacity), ownsBuffer(ownsBuffer)
    {
    }

    ~RSBuffer()
    {
        if (ownsBuffer && buffer != nullptr)
        {
            delete[] buffer;
            buffer = nullptr;
            capacity = 0;
            ownsBuffer = false;
        }
    }

    void writeByte(int value)
    {
        buffer[writePosition++] = static_cast<char>(value);
    }

    void writeShort(int value)
    {
        buffer[writePosition++] = static_cast<char>(value >> 8);
        buffer[writePosition++] = static_cast<char>(value);
    }

    void writeInt(int value)
    {
        buffer[writePosition++] = static_cast<char>(value >> 24);
        buffer[writePosition++] = static_cast<char>(value >> 16);
        buffer[writePosition++] = static_cast<char>(value >> 8);
        buffer[writePosition++] = static_cast<char>(value);
    }

    void writeLong(long long value)
    {
        buffer[writePosition++] = static_cast<char>(value >> 56);
        buffer[writePosition++] = static_cast<char>(value >> 48);
        buffer[writePosition++] = static_cast<char>(value >> 40);
        buffer[writePosition++] = static_cast<char>(value >> 32);
        buffer[writePosition++] = static_cast<char>(value >> 24);
        buffer[writePosition++] = static_cast<char>(value >> 16);
        buffer[writePosition++] = static_cast<char>(value >> 8);
        buffer[writePosition++] = static_cast<char>(value);
    }

    void writeString(const std::string &value)
    {
        for (char c: value)
        {
            buffer[writePosition++] = c;
        }
        buffer[writePosition++] = 0;
    }

    [[nodiscard]] bool canRead(size_t n) const
    {
        return readPosition <= writePosition && (writePosition - readPosition) >= n;
    }

    unsigned char readUnsignedByte()
    {
        if (!canRead(1)) return 0;
        return static_cast<unsigned char>(buffer[readPosition++]);
    }

    char readByte()
    {
        if (!canRead(1)) return 0;
        return buffer[readPosition++];
    }

    int readUnsignedShort()
    {
        if (!canRead(2)) { readPosition = writePosition; return 0; }
        int byte1 = buffer[readPosition++] & 0xff;
        int byte2 = buffer[readPosition++] & 0xff;
        return (byte1 << 8) | byte2;
    }

    int readSmartInt()
    {
        if (!canRead(1)) return 0;
        if (buffer[readPosition] < 0)
        {
            return readInt() & 0x7FFFFFFF;
        }
        return readUnsignedShort();
    }

    // Same 2-or-4 byte encoding as readSmartInt(), but the 2-byte sentinel
    // 32767 means "absent" and decodes to -1.
    int readBigSmart()
    {
        if (!canRead(1)) return 0;
        if (buffer[readPosition] < 0)
        {
            return readInt() & 0x7FFFFFFF;
        }
        int value = readUnsignedShort();
        return value == 32767 ? -1 : value;
    }

    int readSmart()
    {
        if (!canRead(1)) return 0;
        if ((buffer[readPosition] & 0xFF) < 128)
        {
            return readUnsignedByte();
        }
        return readUnsignedShort() - 32768;
    }

    int readInt()
    {
        if (!canRead(4)) { readPosition = writePosition; return 0; }
        int byte1 = buffer[readPosition++] & 0xff;
        int byte2 = buffer[readPosition++] & 0xff;
        int byte3 = buffer[readPosition++] & 0xff;
        int byte4 = buffer[readPosition++] & 0xff;
        return (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
    }

    int readMediumInt()
    {
        if (!canRead(3)) { readPosition = writePosition; return 0; }
        int byte1 = buffer[readPosition++] & 0xff;
        int byte2 = buffer[readPosition++] & 0xff;
        int byte3 = buffer[readPosition++] & 0xff;
        return (byte1 << 16) | (byte2 << 8) | byte3;
    }

    long long readLong()
    {
        if (!canRead(8)) { readPosition = writePosition; return 0; }
        return  (long long) (buffer[readPosition++] & 0xff) << 56 |
                (long long) (buffer[readPosition++] & 0xff) << 48 |
                (long long) (buffer[readPosition++] & 0xff) << 40 |
                (long long) (buffer[readPosition++] & 0xff) << 32 |
                (long long) (buffer[readPosition++] & 0xff) << 24 |
                (long long) (buffer[readPosition++] & 0xff) << 16 |
                (long long) (buffer[readPosition++] & 0xff) << 8  |
                (long long) (buffer[readPosition++] & 0xff);
    }

    std::string readString()
    {
        static constexpr uint16_t cp1252_80_9f[32] = {
            0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
            0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
        };
        std::string value;
        unsigned char c;
        while (readPosition < writePosition &&
               (c = static_cast<unsigned char>(buffer[readPosition++])) != 0)
        {
            uint32_t cp;
            if (c < 0x80) { value += static_cast<char>(c); continue; }
            if (c < 0xA0)
            {
                cp = cp1252_80_9f[c - 0x80];
                if (cp == 0) cp = 0xFFFD;
            }
            else
            {
                cp = c;
            }
            if (cp < 0x80)
            {
                value += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                value += static_cast<char>(0xC0 | (cp >> 6));
                value += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                value += static_cast<char>(0xE0 | (cp >> 12));
                value += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                value += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        return value;
    }

    int readVarInt()
    {
        int result = 0;
        int shift = 0;
        int b;
        do
        {
            if (remaining() < 1 || shift >= 35)
                return result;
            b = readUnsignedByte();
            result |= (b & 0x7F) << shift;
            shift += 7;
        } while (b > 127);
        return result;
    }

    float readFloat()
    {
        int bits = readInt();
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    void skip(int length)
    {
        if (length < 0) return;
        if (!canRead(static_cast<size_t>(length))) { readPosition = writePosition; return; }
        readPosition += length;
    }

    [[nodiscard]] unsigned int size() const
    {
        return writePosition;
    }

    [[nodiscard]] size_t remaining() const
    {
        return readPosition >= writePosition ? 0 : (writePosition - readPosition);
    }

    char read(char *dest, size_t dest_size)
    {
        size_t bytesAvailable = remaining();
        if (dest_size > bytesAvailable)
        {
            return 1;
        }
        std::memcpy(dest, buffer + readPosition, dest_size);
        readPosition += dest_size;
        return 0;
    }

    void writeFully(char *buf, size_t size)
    {
        if (ownsBuffer && buffer != nullptr)
        {
            delete[] buffer;
        }
        buffer = new char[size];
        writePosition = static_cast<unsigned int>(size);
        capacity = size;
        readPosition = 0;
        ownsBuffer = true;
        std::memcpy(buffer, buf, size);
    }

    char write(const char *src, size_t src_size)
    {
        if (writePosition + src_size > capacity)
        {
            return 1;
        }
        std::memcpy(buffer + writePosition, src, src_size);
        writePosition += static_cast<unsigned int>(src_size);
        return 0;
    }

    [[nodiscard]] RSBuffer subBuffer(size_t start, size_t length) const
    {
        if (start >= writePosition || start + length > writePosition || length == 0)
        {
            return RSBuffer(nullptr, 0);
        }
        return {buffer + start, length, static_cast<unsigned int>(length), 0u, false};
    }
};
