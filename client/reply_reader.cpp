#include "reply_reader.h"

#include <cerrno>
#include <charconv>
#include <sys/socket.h>

namespace {

bool to_number(const std::string& text, long long& out) {
    const char* end = text.data() + text.size();
    std::from_chars_result result = std::from_chars(text.data(), end, out);
    return result.ec == std::errc() && result.ptr == end;
}

}  // namespace

bool ReplyReader::fill_buffer() {
    // Throw away the part we've already parsed so the buffer doesn't grow forever.
    if (pos_ > 0) {
        buffer_.erase(0, pos_);
        pos_ = 0;
    }
    char chunk[16 * 1024];
    while (true) {
        ssize_t n = recv(fd_, chunk, sizeof(chunk), 0);
        if (n > 0) {
            buffer_.append(chunk, static_cast<size_t>(n));
            return true;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;  // 0 = server closed the connection, <0 = error
    }
}

bool ReplyReader::read_line(std::string& line) {
    while (true) {
        size_t end = buffer_.find("\r\n", pos_);
        if (end != std::string::npos) {
            line = buffer_.substr(pos_, end - pos_);
            pos_ = end + 2;
            return true;
        }
        if (!fill_buffer()) return false;
    }
}

bool ReplyReader::read_exact(size_t count, std::string& data) {
    while (buffer_.size() - pos_ < count) {
        if (!fill_buffer()) return false;
    }
    data = buffer_.substr(pos_, count);
    pos_ += count;
    return true;
}

bool ReplyReader::read_reply(Reply& reply) {
    std::string line;
    if (!read_line(line) || line.empty()) return false;

    char type = line[0];
    std::string rest = line.substr(1);
    reply = Reply{};

    switch (type) {
        case '+':
            reply.type = Reply::Type::Status;
            reply.text = rest;
            return true;
        case '-':
            reply.type = Reply::Type::Error;
            reply.text = rest;
            return true;
        case ':':
            reply.type = Reply::Type::Integer;
            return to_number(rest, reply.integer);
        case '$': {
            long long length;
            if (!to_number(rest, length)) return false;
            if (length < 0) {
                reply.type = Reply::Type::Nil;
                return true;
            }
            reply.type = Reply::Type::Bulk;
            std::string crlf;
            return read_exact(static_cast<size_t>(length), reply.text) && read_exact(2, crlf);
        }
        case '*': {
            long long count;
            if (!to_number(rest, count)) return false;
            if (count < 0) {
                reply.type = Reply::Type::Nil;
                return true;
            }
            reply.type = Reply::Type::Array;
            reply.elements.resize(static_cast<size_t>(count));
            for (Reply& element : reply.elements) {
                if (!read_reply(element)) return false;  // recursion: arrays can nest
            }
            return true;
        }
        default:
            return false;  // not a RESP type byte
    }
}
