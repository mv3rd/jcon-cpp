#include "json_rpc_server.h"
#include "jcon_assert.h"
#include "json_rpc_endpoint.h"
#include "json_rpc_error.h"
#include "json_rpc_file_logger.h"
#include "string_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QMetaMethod>

namespace {
    QString logInvoke(const QMetaMethod& meta_method,
                      const QVariantList& args,
                      const QVariant& return_value);
}

namespace jcon {

const QString JsonRpcServer::InvalidRequestId = "";

JsonRpcServer::JsonRpcServer(QObject* parent,
                             std::shared_ptr<JsonRpcLogger> logger)
    : QObject(parent)
    , m_logger(logger)
{
    if (!m_logger) {
        m_logger = std::make_shared<JsonRpcFileLogger>("json_server_log.txt");
    }
}

JsonRpcServer::~JsonRpcServer()
{
}

void JsonRpcServer::registerServices(const QObjectList& services)
{
    m_services = services;
}

void JsonRpcServer::jsonRequestReceived(const QJsonObject& request,
                                        QObject* socket)
{
    JCON_ASSERT(request.value("jsonrpc").toString() == "2.0");

    if (request.value("jsonrpc").toString() != "2.0") {
        logError("invalid protocol tag");
        return;
    }

    QString method_name = request.value("method").toString();
    if (method_name.isEmpty()) {
        logError("no method present in request");
    }

    QVariant params = request.value("params").toVariant();

    QString request_id = request.value("id").toString(InvalidRequestId);

    QVariant return_value;
    if (!dispatch(method_name, params, request_id, return_value)) {
        auto msg = QString("method '%1' not found, check name and "
                           "parameter types ").arg(method_name);
        logError(msg);

        // send error response if request had valid ID
        if (request_id != InvalidRequestId) {
            QJsonDocument error =
                createErrorResponse(request_id,
                                    JsonRpcError::EC_MethodNotFound,
                                    msg);

            JsonRpcEndpoint* endpoint = findClient(socket);
            if (!endpoint) {
                logError("invalid client socket, cannot send response");
                return;
            }

            endpoint->send(error);
            return;
        }
    }

    // send response if request had valid ID
    if (request_id != InvalidRequestId) {
        QJsonDocument response = createResponse(request_id,
                                                return_value,
                                                method_name);

        JsonRpcEndpoint* endpoint = findClient(socket);
        if (!endpoint) {
            logError("invalid client socket, cannot send response");
            return;
        }

        endpoint->send(response);
    }
}

bool JsonRpcServer::dispatch(const QString& method_name,
                             const QVariant& params,
                             const QString& request_id,
                             QVariant& return_value)
{
    for (auto& s : m_services) {
        const QMetaObject* meta_obj = s->metaObject();
        for (int i = 0; i < meta_obj->methodCount(); ++i) {
            auto meta_method = meta_obj->method(i);
            if (meta_method.name() == method_name) {
                JCON_ASSERT(params.type() == QVariant::List || params.type() == QVariant::StringList);
                if (call(s, meta_method, params.toList(), return_value)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool JsonRpcServer::call(QObject* object,
                         const QMetaMethod& meta_method,
                         const QVariantList& args,
                         QVariant& return_value)
{
    return_value = QVariant();

    if (!convertArgs(meta_method, args)) {
        return false;
    }

    return doCall(object, meta_method, args, return_value);
}

bool JsonRpcServer::convertArgs(const QMetaMethod& meta_method,
                                const QVariantList& args)
{
    QList<QByteArray> param_types = meta_method.parameterTypes();
    if (args.size() != param_types.size()) {
        logError(QString("wrong number of arguments to method %1 -- "
                         "expected %2 arguments, but got %3")
                 .arg(QString(meta_method.methodSignature()))
                 .arg(meta_method.parameterCount())
                 .arg(args.size()));
        return false;
    }

    for (int i = 0; i < param_types.size(); i++) {
        const QVariant& arg = args.at(i);
        if (!arg.isValid()) {
            logError(QString("argument %1 of %2 to method %3 is invalid")
                     .arg(i + 1)
                     .arg(param_types.size())
                     .arg(QString(meta_method.methodSignature())));
            return false;
        }
    }
    return true;
}

// based on https://gist.github.com/andref/2838534.
bool JsonRpcServer::doCall(QObject* object,
                           const QMetaMethod& meta_method,
                           const QVariantList& args,
                           QVariant& return_value)
{
    QList<QGenericArgument> arguments;

    for (auto& argument : args) {
        arguments << QGenericArgument(
            QMetaType::typeName(QMetaType::QVariant),
            &argument);
    }

    const char* return_type_name = meta_method.typeName();
    int return_type = QMetaType::type(return_type_name);
    JCON_ASSERT(return_type != QMetaType::UnknownType);
    if (return_type != QMetaType::Void) {
        return_value = QVariant(return_type, nullptr);
    }

    QGenericReturnArgument return_argument(
        return_type_name,
        return_type != QMetaType::QVariant ? const_cast<void*>(return_value.constData()) : &return_value
    );

    // perform the call
    bool ok = meta_method.invoke(
        object,
        Qt::DirectConnection,
        return_argument,
        arguments.value(0),
        arguments.value(1),
        arguments.value(2),
        arguments.value(3),
        arguments.value(4),
        arguments.value(5),
        arguments.value(6),
        arguments.value(7),
        arguments.value(8),
        arguments.value(9)
    );

    if (!ok) {
        // qDebug() << "calling" << meta_method.methodSignature() << "failed.";
        return false;
    }

    logInfo(logInvoke(meta_method, args, return_value));

    return true;
}

QJsonDocument JsonRpcServer::createResponse(const QString& request_id,
                                            const QVariant& return_value,
                                            const QString& method_name)
{
    QVariantMap res_json_obj {
        { "jsonrpc", "2.0" },
        { "id", request_id },
        { "result", return_value }
    };

    return QJsonDocument::fromVariant(res_json_obj);
}

QJsonDocument JsonRpcServer::createErrorResponse(const QString& request_id,
                                                 int code,
                                                 const QString& message)
{
    QJsonObject error_object {
        { "code", code },
        { "message", message }
    };

    QJsonObject res_json_obj {
        { "jsonrpc", "2.0" },
        { "error", error_object },
        { "id", request_id }
    };
    return QJsonDocument(res_json_obj);
}

void JsonRpcServer::logInfo(const QString& msg)
{
    m_logger->logInfo("JSON RPC server: " + msg);
}

void JsonRpcServer::logError(const QString& msg)
{
    m_logger->logError("JSON RPC server error: " + msg);
}

}

namespace {

QString logInvoke(const QMetaMethod& meta_method,
                  const QVariantList& args,
                  const QVariant& return_value)
{
    const auto ns = meta_method.parameterNames();
    auto ps = jcon::variantListToStringList(args);
    QStringList args_sl;
    std::transform(ns.begin(), ns.end(), ps.begin(),
                   std::back_inserter(args_sl),
                   [](const decltype(*ns.begin())& x, const decltype(*ps.begin())& y) -> QString {
                       return static_cast<QString>(x) + ": " + y;
                   }
        );

    auto msg = QString("%1 invoked ")
        .arg(static_cast<QString>(meta_method.name()));

    if (args_sl.empty()) {
        msg += "without arguments";
    } else {
        msg += QString("with argument%1: %2")
            .arg(args_sl.size() == 1 ? "" : "s")
            .arg(args_sl.join(", "));
    }

    if (return_value.isValid()) {
        msg += " -> returning: " + jcon::variantToString(return_value);
    }

    return msg;
}

}


#include "json_rpc_server.moc"
