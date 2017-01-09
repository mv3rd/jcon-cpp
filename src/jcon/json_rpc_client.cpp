#include "json_rpc_client.h"
#include "json_rpc_file_logger.h"
#include "json_rpc_success.h"
#include "jcon_assert.h"
#include "string_util.h"

#include <QSignalSpy>
#include <QUuid>

#include <memory>

namespace jcon {

const QString JsonRpcClient::InvalidRequestId = "";

JsonRpcClient::JsonRpcClient(std::shared_ptr<JsonRpcSocket> socket,
                             QObject* parent,
                             std::shared_ptr<JsonRpcLogger> logger)
    : QObject(parent)
    , m_logger(logger)
{
    if (!m_logger) {
        m_logger = std::make_shared<JsonRpcFileLogger>("json_client_log.txt");
    }

    m_endpoint = std::make_shared<JsonRpcEndpoint>(socket, m_logger, this);

    connect(m_endpoint.get(), &JsonRpcEndpoint::socketConnected,
            this, &JsonRpcClient::socketConnected);

    connect(m_endpoint.get(), &JsonRpcEndpoint::socketDisconnected,
            this, &JsonRpcClient::socketDisconnected);

    connect(m_endpoint.get(), &JsonRpcEndpoint::socketError,
            this, &JsonRpcClient::socketError);
}

JsonRpcClient::~JsonRpcClient()
{
    disconnectFromServer();
}

std::shared_ptr<JsonRpcResult>
JsonRpcClient::waitForSyncCallbacks(const JsonRpcRequest* request)
{
    m_last_result = QVariant();
    m_last_error = JsonRpcError();

    connect(request, &JsonRpcRequest::result,
            this, &JsonRpcClient::syncCallResult);

    connect(request, &JsonRpcRequest::error,
            this, &JsonRpcClient::syncCallError);

    QSignalSpy res_spy(this, &JsonRpcClient::syncCallSucceeded);
    QSignalSpy err_spy(this, &JsonRpcClient::syncCallFailed);
    QTime timer;
    timer.start();
    while (res_spy.isEmpty() && err_spy.isEmpty() &&
           timer.elapsed() < CallTimeout) {
        QCoreApplication::processEvents();
    }
    if (!res_spy.isEmpty()) {
        return std::make_shared<JsonRpcSuccess>(m_last_result);
    } else if (!err_spy.isEmpty()) {
        return std::make_shared<JsonRpcError>(m_last_error);
    } else {
        return std::make_shared<JsonRpcError>(
            JsonRpcError::EC_InternalError,
            "RPC call timed out"
        );
    }
}

std::shared_ptr<JsonRpcResult>
JsonRpcClient::callExpandArgs(const QString& method, const QVariantList& args)
{
    auto req = callAsyncExpandArgs(method, args);
    return waitForSyncCallbacks(req.get());
}

std::shared_ptr<JsonRpcRequest>
JsonRpcClient::callAsyncExpandArgs(const QString& method,
                                   const QVariantList& args)
{
    std::shared_ptr<JsonRpcRequest> request;
    QJsonObject req_json_obj;
    std::tie(request, req_json_obj) = prepareCall(method);

    if (!args.empty()) {
        req_json_obj["params"] = QJsonArray::fromVariantList(args);
    }

    m_logger->logInfo(getCallLogMessage(method, args));
    m_endpoint->send(QJsonDocument(req_json_obj));

    return request;
}

std::pair<std::shared_ptr<JsonRpcRequest>, QJsonObject>
JsonRpcClient::prepareCall(const QString& method)
{
    std::shared_ptr<JsonRpcRequest> request;
    RequestId id;
    std::tie(request, id) = createRequest();
    m_outstanding_requests[id] = request;
    QJsonObject req_json_obj = createRequestJsonObject(method, id);
    return std::make_pair(request, req_json_obj);
}

std::pair<std::shared_ptr<JsonRpcRequest>, JsonRpcClient::RequestId>
JsonRpcClient::createRequest()
{
    auto id = createUuid();
    auto request = std::make_shared<JsonRpcRequest>(this, id);
    return std::make_pair(request, id);
}

JsonRpcClient::RequestId JsonRpcClient::createUuid()
{
    RequestId id = QUuid::createUuid().toString();
    int len = id.length();
    id = id.left(len - 1).right(len - 2);
    return id;
}

QJsonObject JsonRpcClient::createRequestJsonObject(const QString& method,
                                                   const QString& id)
{
    return QJsonObject {
        { "jsonrpc", "2.0" },
        { "method", method },
        { "id", id }
    };
}

bool JsonRpcClient::connectToServer(const QString& host, int port)
{
    if (!m_endpoint->connectToHost(host, port)) {
        return false;
    }

    connect(m_endpoint.get(), &JsonRpcEndpoint::jsonObjectReceived,
            this, &JsonRpcClient::jsonResponseReceived);

    return true;
}

void JsonRpcClient::disconnectFromServer()
{
    m_endpoint->disconnectFromHost();
    m_endpoint->disconnect(this);
}

bool JsonRpcClient::isConnected() const
{
    return m_endpoint->isConnected();
}

QHostAddress JsonRpcClient::clientAddress() const
{
    return m_endpoint->localAddress();
}

int JsonRpcClient::clientPort() const
{
    return m_endpoint->localPort();
}

QHostAddress JsonRpcClient::serverAddress() const
{
    return m_endpoint->peerAddress();
}

int JsonRpcClient::serverPort() const
{
    return m_endpoint->peerPort();
}

void JsonRpcClient::syncCallResult(const QVariant& result)
{
    m_last_result = result;
    emit syncCallSucceeded();
}

void JsonRpcClient::syncCallError(int code,
                                  const QString& message,
                                  const QVariant& data)
{
    m_last_error = JsonRpcError(code, message, data);
    emit syncCallFailed();
}

void JsonRpcClient::jsonResponseReceived(const QJsonObject& response)
{
    JCON_ASSERT(response["jsonrpc"].toString() == "2.0");

    if (response.value("jsonrpc").toString() != "2.0") {
        logError("invalid protocol tag");
        return;
    }

    if (response.value("error").isObject()) {
        int code;
        QString msg;
        QVariant data;
        getJsonErrorInfo(response, code, msg, data);
        logError(QString("(%1) - %2").arg(code).arg(msg));

        RequestId id = response.value("id").toString(InvalidRequestId);
        if (id != InvalidRequestId) {
            auto it = m_outstanding_requests.find(id);
            if (it == m_outstanding_requests.end()) {
                logError(QString("got error response for non-existing "
                                 "request: %1").arg(id));
                return;
            }
            emit it->second->error(code, msg, data);
            m_outstanding_requests.erase(it);
        }

        return;
    }

    if (response["result"].isUndefined()) {
        logError("result is undefined");
        return;
    }

    RequestId id = response.value("id").toString(InvalidRequestId);
    if (id == InvalidRequestId) {
        logError("response ID is undefined");
        return;
    }

    QVariant result = response.value("result").toVariant();

    auto it = m_outstanding_requests.find(id);
    if (it == m_outstanding_requests.end()) {
        logError(QString("got response to non-existing request: %1").arg(id));
        return;
    }

    emit it->second->result(result);
    m_outstanding_requests.erase(it);
}

void JsonRpcClient::getJsonErrorInfo(const QJsonObject& response,
                                     int& code,
                                     QString& message,
                                     QVariant& data)
{
    QJsonObject error = response["error"].toObject();
    code = error["code"].toInt();
    message = error["message"].toString("unknown error");
    data = error.value("data").toVariant();
}

QString JsonRpcClient::getCallLogMessage(const QString& method,
                                         const QVariantList& args)
{
    auto msg = QString("Calling RPC method: '%1' ").arg(method);
    if (args.empty()) {
        msg += "without arguments";
    } else {
        msg += QString("with argument%1: %2")
            .arg(args.size() == 1 ? "" : "s")
            .arg(variantListToStringList(args).join(", "));
    }
    return msg;
}

void JsonRpcClient::logError(const QString& msg)
{
    m_logger->logError("JSON RPC client error: " + msg);
}

}


#include "json_rpc_client.moc"
