#include<string>
#include<mutex>
#include<unordered_map>
#include<shared_mutex>
#include<optional>

class SessionManager {
public:
    static SessionManager& getInstance() {
        static SessionManager instance;
        return instance;
    }
    
    void addToken(const std::string& token, const std::string& username) {
        std::unique_lock lock(mutex_);  // 写锁
        sessions_[token] = username;
    }

    bool queryToken(const std::string& token) const {
        std::shared_lock lock(mutex_); // 获取共享锁（读锁）
        auto it = sessions_.find(token);
        return it != sessions_.end();
    }
    std::optional<std::string> getSessionValue(const std::string& token) const {
        std::shared_lock lock(mutex_);
        auto it = sessions_.find(token);
        return it != sessions_.end() ? std::optional<std::string>(it->second) : std::nullopt;
    }
    // 删除会话（写操作）
    bool removeToken(const std::string& token) {
        std::unique_lock lock(mutex_);  // 写锁
        return sessions_.erase(token) > 0;
    }

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
private:
    SessionManager() = default;  // 单例构造函数
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> sessions_;
};