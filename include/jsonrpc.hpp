#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include <atomic>
#include <mutex>

// Shared memory
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// TCP
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace jsonrpc {

using Json = nlohmann::json;

constexpr std::string_view JSONRPC_VERSION_2_0 = "2.0";
constexpr std::int32_t JSONRPC_ERR_INVALID_REQUEST = -32600;

namespace detail {
    inline bool has_key(const Json& v, std::string_view key) {
        return v.find(key) != v.end();
    }
} // namespace detail

namespace shared_memory {
    constexpr size_t MAX_BUFFER_SIZE = 4096;

    struct shmbuf {
        sem_t sem1 = {};
        sem_t sem2 = {};
        size_t used = 0;
        uint8_t buf[MAX_BUFFER_SIZE] = {};
    };

    class channel {
    public:
        struct connection {
            int fd = -1;
            void* mem = nullptr;
            std::size_t size = 0;
            std::string name;
        };

        static auto create(std::string_view path) -> std::optional<connection> {
            int fd = shm_open(std::data(path), O_CREAT | O_EXCL | O_RDWR, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
            if (fd == -1) {
                return {};
            }

            if (ftruncate(fd, sizeof(struct shmbuf)) == -1) {
                return {};
            }

            shmbuf* shmp = static_cast<shmbuf*>(mmap(nullptr, sizeof(*shmp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
            if (shmp == MAP_FAILED) {
                return {};
            }

            if (sem_init(&shmp->sem1, 1, 0) == -1) {
                return {};
            }
            if (sem_init(&shmp->sem2, 1, 0) == -1) {
                return {};
            }

            connection conn;
            conn.fd = fd;
            conn.mem = shmp;
            conn.name = path;
            conn.size = sizeof(*shmp);

            return { conn };
        }

        static auto destroy(connection& conn) {
            shmbuf* shmp = static_cast<shmbuf*>(conn.mem);

            sem_destroy(&shmp->sem1);
            sem_destroy(&shmp->sem2);

            shm_unlink(std::data(conn.name));
        }

        static auto connect(std::string_view path) -> std::optional<connection> {
            int fd = shm_open(std::data(path), O_RDWR, 0);
            if (fd == -1) {
                perror("shm_open");
                return {};
            }

            shmbuf* shmp = static_cast<shmbuf*>(mmap(nullptr, sizeof(*shmp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
            if (shmp == MAP_FAILED) {
                return {};
            }

            connection conn;
            conn.fd = fd;
            conn.mem = shmp;
            conn.name = path;
            conn.size = sizeof(*shmp);

            return { conn };
        }

        static auto disconnect([[maybe_unused]] connection& conn) {
        }

        static auto send(connection& conn, const uint8_t* buf, size_t size) {
            shmbuf* shmp = static_cast<shmbuf*>(conn.mem);

            if (sem_wait(&shmp->sem2) == -1) { }

            shmp->used = size;
            memcpy(&shmp->buf, buf, size);

            if (sem_post(&shmp->sem1) == -1) { }
        }

        static auto recv(connection& conn, uint8_t* buf, [[maybe_unused]] size_t size, size_t& readden) {
            shmbuf* shmp = static_cast<shmbuf*>(conn.mem);

            if (sem_wait(&shmp->sem1) == -1) { }

            memcpy(buf, shmp->buf, shmp->used);
            readden = shmp->used;

            if (sem_post(&shmp->sem2) == -1) { }
        }
    };

} // namespace shared_memory

namespace tcp {

    constexpr char DefaultServicePort[] = "49192";
    constexpr char DefaultServiceAddress[] = "localhost";
    constexpr int DefaultPollTimeout = -1;

    namespace detail {

        inline std::string ip_string(const struct sockaddr* sa) {
            char str[INET_ADDRSTRLEN];
            const auto maxlen = sizeof str;

            switch (sa->sa_family) {
            case AF_INET:
                inet_ntop(AF_INET, &(((struct sockaddr_in*)sa)->sin_addr), str, maxlen);
                break;

            case AF_INET6:
                inet_ntop(AF_INET6, &(((struct sockaddr_in6*)sa)->sin6_addr), str, maxlen);
                break;

            default:
                return {};
            }

            return std::string { str };
        }

        inline auto listen_socket(std::string_view address, std::string_view port) {
            constexpr int BACKLOG = 10;

            addrinfo hints = {};
            addrinfo* servinfo = nullptr;
            addrinfo* p = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE;

            if (const auto rc = getaddrinfo(std::data(address), std::data(port), &hints, &servinfo); rc != 0) {
                // LOG_ERROR( "getaddrinfo {}", gai_strerror( rc ) );
                return -1;
            }

            int sockfd = 0;
            for (p = servinfo; p != nullptr; p = p->ai_next) {
                if (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol); sockfd == -1) {
                    // LOG_WARN( "socket {}", strerror( errno ) );
                    continue;
                }

                int opt = 1;
                if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) == -1) {
                    // LOG_ERROR( "setsockopt {}", strerror( errno ) );
                    return -1;
                }

                if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                    close(sockfd);
                    // LOG_ERROR( "bind {}", strerror( errno ) );
                    continue;
                }

                break;
            }

            if (p == nullptr) {
                // LOG_ERROR( "Failed to bind any address {}", strerror( errno ) );
                freeaddrinfo(servinfo);
                return -1;
            }

            // LOG_INFO( "Tcp Service: start on '{}:{}'", detail::ip_string( servinfo->ai_addr ), port );

            freeaddrinfo(servinfo);

            if (listen(sockfd, BACKLOG) == -1) {
                // LOG_ERROR( "listen {}", strerror( errno ) );
                return -1;
            }

            return sockfd;
        }

        inline auto accept_socket(int serv) {
            struct sockaddr_storage remoteaddr;
            socklen_t addrlen = sizeof remoteaddr;
            const int newfd = accept(serv, reinterpret_cast<struct sockaddr*>(&remoteaddr), &addrlen);
            if (newfd == -1) {
                // LOG_ERROR( "accept {}", strerror( errno ) );
                return -1;
            }

            // LOG_INFO( "Tcp Service: accept connection from {} socket {}",
            //     detail::ip_string( reinterpret_cast<struct sockaddr*>( &remoteaddr ) ), newfd );

            return newfd;
        }

        inline auto connect_socket(std::string_view address, std::string_view port) {
            struct addrinfo hints = {}, *servinfo = nullptr, *p = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            if (const auto rc = getaddrinfo(std::data(address), std::data(port), &hints, &servinfo); rc != 0) {
                // LOG_ERROR( "getaddrinfo {}", gai_strerror( rc ) );
                return -1;
            }

            int sockfd = 0;
            for (p = servinfo; p != NULL; p = p->ai_next) {
                if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
                    // LOG_WARN( "socket {}", strerror( errno ) );
                    continue;
                }

                if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                    close(sockfd);
                    // LOG_ERROR( "connect {}", strerror( errno ) );

                    continue;
                }

                break;
            }

            if (p == nullptr) {
                // LOG_ERROR( "Failed to connect {}", strerror( errno ) );
                freeaddrinfo(servinfo);
                return -1;
            }

            // LOG_INFO( "Tcp Client: connecting to '{}:{}'", detail::ip_string( servinfo->ai_addr ), port );

            freeaddrinfo(servinfo);

            return sockfd;
        }

    } // namespace detail

    class channel {
    public:
        using byte_buffer = std::vector<uint8_t>;

        struct connection {
            int sockfd = 0;
            bool is_service = false;
            std::vector<struct pollfd> pfds;
        };

        struct request_buffer {
            int32_t cleint = 0;
            size_t offset = 0;
            byte_buffer buffer;
        };

        static auto create(std::string_view address, std::string_view port) -> std::optional<connection> {
            const auto servfd = detail::listen_socket(address, port);
            if (servfd == -1) {
                return {};
            }

            connection conn;
            conn.sockfd = servfd;
            conn.is_service = true;
            conn.pfds.reserve(3);
            conn.pfds.resize(1);
            conn.pfds[0].fd = servfd;
            conn.pfds[0].events = POLLIN;

            return conn;
        }

        static auto destroy(connection& conn) {
            close(conn.sockfd);
        }

        static auto connect(std::string_view address, std::string_view port) -> std::optional<connection> {
            const auto sockfd = detail::connect_socket(address, port);
            if (sockfd == -1)
                return {};

            connection conn;
            conn.sockfd = sockfd;
            conn.pfds.resize(1);
            conn.pfds[0].fd = sockfd;
            conn.pfds[0].events = POLLIN;

            return conn;
        }

        static auto disconnect([[maybe_unused]] connection& conn) {
        }

        static auto send(connection& conn, const uint8_t* buf, size_t size) {
            if (::send(conn.sockfd, buf, size, 0) == -1) {
                // LOG_ERROR( "send {}", strerror( errno ) );
            }
        }

        static auto send_all(connection& conn, const uint8_t* buf, size_t* size) {
            int total = 0;
            int bytesleft = static_cast<int>(*size);

            int n;

            while (total < static_cast<int>(*size)) {
                n = ::send(conn.sockfd, buf + total, bytesleft, 0);
                if (n == -1) {
                    break;
                }
                total += n;
                bytesleft -= n;
            }

            *size = static_cast<size_t>(total);
            return n == -1 ? -1 : 0;
        }

        template <typename F> static auto recv(connection& conn, F&& f) {
            if (const int pollCount = poll(std::data(conn.pfds), std::size(conn.pfds), DefaultPollTimeout); pollCount == -1) {
                // LOG_ERROR( "poll {}", strerror( errno ) );
                return false;
            }

            for (size_t i = 0; i < std::size(conn.pfds); i++) {
                if (conn.pfds[i].revents & POLLIN) {
                    if (conn.is_service && conn.pfds[i].fd == conn.sockfd) {
                        const auto newfd = detail::accept_socket(conn.sockfd);
                        if (newfd != -1) { // Append new client
                            struct pollfd pfd = {};
                            pfd.fd = newfd;
                            pfd.events = POLLIN;
                            conn.pfds.push_back(pfd);
                        } else {
                            // LOG_ERROR("accept {}", strerror(errno));
                        }
                    } else {
                        char buf[4096] = {};
                        const int senderfd = conn.pfds[i].fd;
                        const int nbytes = ::recv(senderfd, buf, sizeof buf, 0);
                        if (nbytes <= 0) {
                            if (nbytes == 0) { // Connection closed
                                // LOG_INFO( "Tcp Service: socket {} hung up", senderfd );
                            } else {
                                // LOG_ERROR( "recv {}", strerror( errno ) );
                            }

                            close(senderfd);

                            conn.pfds.erase(std::remove_if(std::begin(conn.pfds), std::end(conn.pfds),
                                                [senderfd](const auto& pfd) { return senderfd == pfd.fd; }),
                                std::end(conn.pfds));
                        } else {
                            byte_buffer buffer;
                            buffer.resize(nbytes);
                            ::memcpy(std::data(buffer), buf, nbytes);

                            connection cli;
                            cli.sockfd = senderfd;

                            f(cli, buffer);
                        }
                    }
                }
            }

            return true;
        }

        static auto recv(connection& conn, std::span<uint8_t> buffer, size_t& readden) {
            if (const int pollCount = poll(std::data(conn.pfds), std::size(conn.pfds), DefaultPollTimeout); pollCount == -1) {
                // LOG_ERROR( "poll {}", strerror( errno ) );
                return 0;
            }

            for (size_t i = 0; i < std::size(conn.pfds); i++) {
                if (conn.pfds[i].revents & POLLIN) {
                    if (conn.is_service && conn.pfds[i].fd == conn.sockfd) {
                        const auto newfd = detail::accept_socket(conn.sockfd);
                        if (newfd != -1) {
                            struct pollfd pfd = {};
                            pfd.fd = newfd;
                            pfd.events = POLLIN;
                            conn.pfds.push_back(pfd);
                        } else {
                            // LOG_ERROR("accept {}", strerror(errno));
                        }
                    } else {
                        const int senderfd = conn.pfds[i].fd;
                        const int nbytes = ::recv(senderfd, std::data(buffer), std::size(buffer), 0);
                        if (nbytes <= 0) {
                            if (nbytes == 0) { // Connection closed
                                // LOG_INFO( "Tcp Service: socket {} hung up", senderfd );
                            } else {
                                // LOG_ERROR( "recv {}", strerror( errno ) );
                            }

                            close(senderfd);

                            conn.pfds.erase(std::remove_if(std::begin(conn.pfds), std::end(conn.pfds),
                                                [senderfd](const auto& pfd) { return senderfd == pfd.fd; }),
                                std::end(conn.pfds));
                        } else {
                            readden = static_cast<size_t>(nbytes);
                        }
                    }
                }
            }

            return 0;
        }
    };

} // namespace tcp

using SharedMemoryChannel = shared_memory::channel;
using TcpChannel = tcp::channel;

namespace detail {

    template <typename> struct function_traits;

    template <typename Function> struct function_traits : function_traits<decltype(&std::remove_reference<Function>::type::operator())> { };

    template <typename ClassType, typename ReturnType, typename... Arguments>
    struct function_traits<ReturnType (ClassType::*)(Arguments...) const> : function_traits<ReturnType (*)(Arguments...)> { };

    template <typename ClassType, typename ReturnType, typename... Arguments>
    struct function_traits<ReturnType (ClassType::*)(Arguments...)> : function_traits<ReturnType (*)(Arguments...)> { };

    template <typename ReturnType, typename... Arguments> struct function_traits<ReturnType (*)(Arguments...)> {
        using result_type = ReturnType;

        using args_type = std::tuple<typename std::decay<Arguments>::type...>;

        template <std::size_t Index> using argument = typename std::tuple_element<Index, std::tuple<Arguments...>>::type;

        static const std::size_t arity = sizeof...(Arguments);
    };

    template <typename T, std::size_t... Indices> auto tuple_pack_helper(const std::vector<T>& v, std::index_sequence<Indices...>) {
        return std::make_tuple(v[Indices]...);
    }

    template <std::size_t N, typename T> auto tuple_pack(const std::vector<T>& v) {
        assert(v.size() >= N);
        return tuple_pack_helper(v, std::make_index_sequence<N>());
    }

    template <typename Functor, typename... Args, std::size_t... I>
    decltype(auto) call_helper(Functor func, std::tuple<Args...>&& params, std::index_sequence<I...>) {
        return func(std::get<I>(params)...);
    }

    template <typename Functor, typename... Args> decltype(auto) call(Functor f, std::tuple<Args...>& args) {
        return call_helper(f, std::forward<std::tuple<Args...>>(args), std::index_sequence_for<Args...> {});
    }

} // namespace detail

using FunctionHolder = std::function<Json(const Json&)>;

template <typename Key = std::string, typename KeyView = std::string_view> class Dispatcher {
public:
    template <typename F> auto bind(KeyView name, F func) {
        using result_type = typename detail::function_traits<F>::result_type;
        using args_type = typename detail::function_traits<F>::args_type;

        _funcs.emplace(name, [func](const auto& params) -> Json {
            if constexpr (std::is_same_v<result_type, void>) {
                if constexpr (detail::function_traits<F>::arity > 1) {
                    detail::call(func, detail::tuple_pack<detail::function_traits<F>::arity, Json>(params));
                } else {
                    auto args_tuple = std::make_tuple(params.template get<std::tuple_element_t<0, args_type>>());
                    detail::call(func, args_tuple);
                }

                return {};
            } else {
                auto args_tuple = detail::tuple_pack<detail::function_traits<F>::arity, Json>(params);
                auto res = detail::call(func, args_tuple);
                return res;
            }
        });
    }

    auto invoke_method(KeyView name, const Json& params) {
        if (auto it = _funcs.find(Key { name }); it != std::end(_funcs)) {
            std::cout << "Method call: " << name << " " << std::endl;
            return it->second(params);
        }

        return Json {};
    }

    auto invoke_notification(KeyView name, const Json& params) {
        if (auto it = _funcs.find(Key { name }); it != std::end(_funcs)) {
            std::cout << "Notification call: " << name << " " << std::endl;
            it->second(params);
        }
    }

    std::unordered_map<Key, FunctionHolder> _funcs;
};

template <typename Channel, typename Connection = typename Channel::connection> class Server {
public:
    static constexpr std::size_t DefaultBufferSize = 4096;

    struct Configuration {
        std::string_view address {};
        std::string_view port {};
    };

    auto handle_request(const Json& req) -> std::optional<Json> {
        Json j;
        Json params;
        std::string method_name;

        if (detail::has_key(req, "params")) {
            params = req["params"];
        }

        if (detail::has_key(req, "method")) {
            method_name = req["method"].get<std::string>();
        }

        if (detail::has_key(req, "id") && !req["id"].is_null()) {
            const auto id = req["id"].get<std::int32_t>();
            const auto res = dispatcher_.invoke_method(method_name, params);
            if (!res.empty()) {
                j["jsonrpc"] = JSONRPC_VERSION_2_0;
                j["id"] = id;
                j["result"] = res;
            }

            return { j };
        } else {
            dispatcher_.invoke_notification(method_name, params);
        }

        return {};
    }

    auto handle_requests(std::string_view req) {
        std::string result;

        try {
            Json j = Json::parse(req);

            if (j.is_object()) {
                auto res = handle_request(j);
                if (res && !res.value().is_null()) {
                    result = res.value().dump();
                }
            } else if (j.is_array()) {
            }

        } catch (Json::parse_error& e) {
            Json j;
            j["jsonrpc"] = JSONRPC_VERSION_2_0;
            j["error"] = { { "code", JSONRPC_ERR_INVALID_REQUEST }, { "message", "Invalid Request" } };
            j["id"] = {};

            result = j.dump();
        }

        if (!result.empty()) {
            return result;
        }

        return std::string {};
    }

    auto run(const Configuration& conf = {}) -> void {
        running_ = true;

        if constexpr (std::is_same_v<Channel, SharedMemoryChannel>) {
            auto c = Channel::create("2baac314-2f68-11eb-adc1-0242ac120002");
            if (c) {
                connection_ = c.value();
            }
        } else if constexpr (std::is_same_v<Channel, TcpChannel>) {
            auto c = Channel::create(
                conf.address.empty() ? tcp::DefaultServiceAddress : conf.address, conf.port.empty() ? tcp::DefaultServicePort : conf.port);
            if (c) {
                connection_ = c.value();
            }
        }

        while (running_) {
            channel_.recv(connection_, [this](auto& client, const auto& buffer) {
                std::string_view s { reinterpret_cast<const char*>(std::data(buffer)), std::size(buffer) };
                std::cout << "--> " << s << std::endl;
                auto res = handle_requests(s);
                if (!res.empty()) {
                    channel_.send(client, reinterpret_cast<const uint8_t*>(std::data(res)), std::size(res));
                    std::cout << "<-- " << res << std::endl;
                }
            });
        }

        Channel::destroy(connection_);
    }

    template <typename F> auto bind(std::string_view name, F&& func) {
        dispatcher_.bind(name, std::forward<F>(func));
    }

    auto cleanup() {
        Channel::destroy(connection_);
    }

    auto quit() -> void {
        running_ = false;
    }

private:
    std::atomic<bool> running_ { false };
    Connection connection_;
    Channel channel_;
    Dispatcher<> dispatcher_;
};

template <typename Channel, typename Connection = typename Channel::connection> class Client {
public:
    Client() {
        if constexpr (std::is_same_v<Channel, SharedMemoryChannel>) {
            auto c = Channel::connect("2baac314-2f68-11eb-adc1-0242ac120002");
            if (c) {
                connection_ = c.value();
            }
        } else if constexpr (std::is_same_v<Channel, TcpChannel>) {
            auto c = Channel::connect(tcp::DefaultServiceAddress, tcp::DefaultServicePort);
            if (c) {
                connection_ = c.value();
            }
        }
    }

    template <typename Arg, typename... Args> auto pack_params(Json& json, Arg&& arg) {
        json.push_back(arg);
    }
    template <typename Arg, typename... Args> auto pack_params(Json& json, Arg&& arg, Args&&... args) {
        json.push_back(arg);
        pack_params(json, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args> auto call(std::string_view name, Args&&... args) -> T {
        Json params;
        pack_params(params, std::forward<Args>(args)...);
        Json j;
        j["jsonrpc"] = JSONRPC_VERSION_2_0;
        j["method"] = name;
        j["params"] = params;
        j["id"] = ++request_id;

        send(j.dump());
        std::cout << "<-- " << j.dump() << std::endl;
        recv([&]([[maybe_unused]] auto& client, const auto& buffer) {
            std::string_view s { reinterpret_cast<const char*>(std::data(buffer)), std::size(buffer) };
            std::cout << "--> " << s << std::endl;
            j = Json::parse(buffer);
        });

        if (!j.empty() && j.find("result") != j.end()) {
            return j["result"].get<T>();
        }

        return T {};
    }

    template <typename F, typename... Args> auto async_call(std::string_view name, [[maybe_unused]] F&& f, Args&&... args) -> void {
        Json params;
        pack_params(params, std::forward<Args>(args)...);
        Json j;
        j["jsonrpc"] = JSONRPC_VERSION_2_0;
        j["method"] = name;
        j["params"] = params;
        j["id"] = ++request_id;

        send(j.dump());
        std::cout << "<-- " << j.dump() << std::endl;

        dispatcher_.bind(request_id, f);
    }

    template <typename... Args> auto notify(std::string_view name, Args&&... args) {
        Json params;
        pack_params(params, std::forward<Args>(args)...);
        Json j;
        j["jsonrpc"] = JSONRPC_VERSION_2_0;
        j["method"] = name;
        j["params"] = params;

        send(j.dump());

        std::cout << "--> " << j.dump() << std::endl;
    }

    auto send(std::string_view buf) {
        channel_.send(connection_, reinterpret_cast<const uint8_t*>(std::data(buf)), std::size(buf));
    }

    template <typename F> auto recv(F&& f) {
        channel_.recv(connection_, std::forward<F>(f));
    }

    auto run() -> void {
        running_ = true;

        while (running_) {
            std::array<uint8_t, 4096> buf;
            size_t readden { 0 };
            channel_.recv(connection_, buf, readden);

            if (readden != 0) {
                std::cout << "Readden" << readden << std::endl;

                auto end = std::begin(buf);
                std::advance(end, readden);

                auto j = Json::parse(std::begin(buf), end);
                if (!j.empty() && j.find("result") != j.end()) {
                    dispatcher_.invoke_method(j["id"].get<std::int32_t>(), j["result"]);
                }
            }
        }
    }

private:
    std::atomic<std::int32_t> request_id { 0 };
    std::atomic<bool> running_ { false };
    Connection connection_;
    Channel channel_;

    Dispatcher<std::int32_t, std::int32_t> dispatcher_;
};

} // namespace jsonrpc