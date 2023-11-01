#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <memory>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/tcp.hpp>

using boost::asio::ip::tcp;

#pragma pack(push, 1)
struct LogEntry {
    char sensor_id[32];
    std::time_t timestamp;
    double value;
};
#pragma pack(pop)

class LogSession : public std::enable_shared_from_this<LogSession> {
public:
    LogSession(tcp::socket socket)
        : socket_(std::move(socket)) {
    }

    void Initiate() {
        StartReading();
    }

private:
    tcp::socket socket_;
    boost::asio::streambuf buffer_;

    bool StartsWith(const std::string& prefix, const std::string& message) {
        return message.compare(0, prefix.length(), prefix) == 0;
    }

    std::vector<std::string> SplitString(const std::string& str, const std::string& delimiter) {
        std::vector<std::string> substrings;
        boost::split(substrings, str, boost::is_any_of(delimiter));
        return substrings;
    }

    std::time_t StringToUnixTime(const std::string& time_string) {
        std::tm tm = {};
        std::istringstream ss(time_string);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return std::mktime(&tm);
    }

    std::string UnixTimeToString(std::time_t time) {
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
        return ss.str();
    }

    void StartReading() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    std::istream is(&buffer_);
                    std::string message(std::istreambuf_iterator<char>(is), {});
                    std::cout << "Received: " << message << std::endl;
                    if (StartsWith("LOG", message)) {
                        std::vector<std::string> splitMessage = SplitString(message, "|");
                        if (splitMessage.size() == 4) {
                            LogEntry log;
                            strncpy(log.sensor_id, splitMessage[1].c_str(), sizeof(log.sensor_id));
                            log.timestamp = StringToUnixTime(splitMessage[2]);
                            log.value = std::stod(splitMessage[3]);
                            std::string fileName = splitMessage[1] + ".dat";
                            std::ofstream file(fileName, std::ios::out | std::ios::binary | std::ios::app);
                            if (file.is_open()) {
                                file.write(reinterpret_cast<char*>(&log), sizeof(LogEntry));
                                file.close();
                            }
                        }
                        StartReading();
                    }
                    else if (StartsWith("GET", message)) {
                        std::vector<std::string> splitMessage = SplitString(message, "|");
                        if (splitMessage.size() == 3) {
                            std::string sensorId = splitMessage[1];
                            int numLogsRequested = std::stoi(splitMessage[2]);
                            std::string fileName = sensorId + ".dat";
                            std::ifstream file(fileName, std::ios::in | std::ios::binary);
                            if (file.is_open()) {
                                file.seekg(0, std::ios::end);
                                int fileSize = file.tellg();
                                file.seekg(0, std::ios::beg);
                                int totalLogs = fileSize / sizeof(LogEntry);
                                int numLogsToRead = std::min(numLogsRequested, totalLogs);
                                std::string response = std::to_string(numLogsToRead);
                                for (int i = 0; i < numLogsToRead; i++) {
                                    LogEntry log;
                                    file.read(reinterpret_cast<char*>(&log), sizeof(LogEntry));
                                    std::string timestampStr = UnixTimeToString(log.timestamp);
                                    std::string valueStr = std::to_string(log.value);
                                    response += ";" + timestampStr + "|" + valueStr;
                                }
                                file.close();
                                WriteResponse(response);
                            }
                            else {
                                WriteResponse("ERROR|INVALID_SENSOR_ID\r\n");
                            }
                        } else {
                            WriteResponse("ERROR|INVALID_REQUEST\r\n");
                        }
                    }
                }
            });
    }

    void WriteResponse(const std::string& message) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(message),
            [this, self, message](boost::system::error_code ec, std::size_t /* length */) {
                if (!ec) {
                    StartReading();
                }
            });
    }
};

class LogServer {
public:
    LogServer(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        StartAccepting();
    }

private:
    tcp::acceptor acceptor_;

    void StartAccepting() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<LogSession>(std::move(socket))->Initiate();
                }
                StartAccepting();
            });
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: log_server <porta>\n";
        return 1;
    }

    boost::asio::io_context io_context;
    LogServer server(io_context, std::atoi(argv[1]));
    io_context.run();

    return 0;
}
