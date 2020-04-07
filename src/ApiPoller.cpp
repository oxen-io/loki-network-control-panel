#include "ApiPoller.hpp"

#include <string>

#include <QObject>

constexpr auto LOKI_DAEMON_URL = "http://localhost:1190/";

// ApiPoller Constructor
ApiPoller::ApiPoller() {
    m_timer = new QTimer();
    m_timer->setInterval(DEFAULT_POLLING_INTERVAL_MS);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ApiPoller::pollDaemon);
    m_timeout_timer = nullptr;
}

// ApiPoller Destructor
ApiPoller::~ApiPoller() {
    delete m_timer;
    m_timer = nullptr;
}

// ApiPoller::setApiEndpoint
void ApiPoller::setApiEndpoint(const QString& endpoint) {
    // we make the same API call every time, so build our payload once
    char buffer[256];
    snprintf(buffer, sizeof(buffer), R"JSON({
            "jsonrpc": "2.0",
            "method": "%s",
            "params": {},
            "id": "empty"
        })JSON",
        endpoint.toStdString().data());
    m_rpcPayload = buffer;
}

// ApiPoller::setIntervalMs
void ApiPoller::setIntervalMs(int intervalMs) {
    m_timer->setInterval(intervalMs);
}

// ApiPoller::startPolling
void ApiPoller::startPolling() {
    m_timer->start();
}

// ApiPoller::stopPolling
void ApiPoller::stopPolling() {
    m_timer->stop();
}

// ApiPoller::pollImmediately
void ApiPoller::pollImmediately() {
    QTimer::singleShot(0, this, &ApiPoller::pollDaemon);
}

// ApiPoller::pollOnce
void ApiPoller::pollOnce() {
    if (m_timeout_timer != nullptr) {
        m_timeout_timer->stop();
        delete m_timeout_timer;
        m_timeout_timer = nullptr;
    }

    this->m_responded = false;
    QTimer::singleShot(500, this, &ApiPoller::pollDaemon);
    m_timeout_timer = new QTimer();
    m_timeout_timer->setInterval(500 * 10);
    m_timeout_timer->setSingleShot(true);
    connect(m_timeout_timer, &QTimer::timeout, this, &ApiPoller::watchDog);
    m_timeout_timer->start();
}

// ApiPoller::pollDaemon
void ApiPoller::pollDaemon() {
    if (m_rpcPayload.empty()) {
        qDebug() << "Warning: No endpoint; call ApiPoller::setApiEndpoint() before polling";
        return;
    }
    m_httpClient.postJson(LOKI_DAEMON_URL, m_rpcPayload, [=](QNetworkReply* reply) {
        static bool lastAttemptWasError = false;
        if (m_timeout_timer != nullptr) {
            m_timeout_timer->stop();
            delete m_timeout_timer;
            m_timeout_timer = nullptr;
        }
        if (this->m_responded) {
            return;
        }
        this->m_responded = true;
        if (reply->error()) {
            if (! lastAttemptWasError) {
                qDebug() << "JSON-RPC error: " << reply->error();
                qDebug() << "         in response to query: " << m_rpcPayload.c_str();
                if (reply->error() > 100)
                {
                    qDebug() << "         server replied: " << reply->readAll();
                }
            }
            lastAttemptWasError = true;
            emit statusAvailable("", reply->error());
        } else {
            lastAttemptWasError = false;
            emit statusAvailable(reply->readAll(), reply->error());
        }
    });
}

void ApiPoller::watchDog() {
    if (!this->m_responded) {
        if (m_timeout_timer != nullptr) {
            m_timeout_timer->stop();
            delete m_timeout_timer;
            m_timeout_timer = nullptr;
        }
        // needs to be before emit, because emit will call pollOnce which sets it
        this->m_responded = true;
        emit statusAvailable("", QNetworkReply::TimeoutError);
    }
}
