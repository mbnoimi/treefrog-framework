/* Copyright (c) 2010-2019, AOYAMA Kazuharu
 * All rights reserved.
 *
 * This software may be used and distributed according to the terms of
 * the New BSD License, which is incorporated herein by reference.
 */

#include "tsystemglobal.h"
#include <QBuffer>
#include <QJsonDocument>
#include <TAppSettings>
#include <THttpRequest>
#include <THttpUtility>
#include <TMultipartFormData>


class MethodHash : public QMap<QString, Tf::HttpMethod> {
public:
    MethodHash() :
        QMap<QString, Tf::HttpMethod>()
    {
        insert("get", Tf::Get);
        insert("head", Tf::Head);
        insert("post", Tf::Post);
        insert("options", Tf::Options);
        insert("put", Tf::Put);
        insert("delete", Tf::Delete);
        insert("trace", Tf::Trace);
        insert("connect", Tf::Connect);
        insert("patch", Tf::Patch);
    }
};
Q_GLOBAL_STATIC(MethodHash, methodHash)


static bool httpMethodOverride()
{
    static bool method = Tf::appSettings()->value(Tf::EnableHttpMethodOverride, false).toBool();
    return (bool)method;
}


/*!
  \class THttpRequestData
  \brief The THttpRequestData class is for shared THttpRequest data objects.
*/

THttpRequestData::THttpRequestData(const THttpRequestData &other) :
    QSharedData(other),
    header(other.header),
    bodyArray(other.bodyArray),
    queryItems(other.queryItems),
    formItems(other.formItems),
    multipartFormData(other.multipartFormData),
    jsonData(other.jsonData),
    clientAddress(other.clientAddress)
{
}

/*!
  \class THttpRequest
  \brief The THttpRequest class contains request information for HTTP.
*/

/*!
  \fn THttpRequest::THttpRequest()
  Constructor.
*/
THttpRequest::THttpRequest() :
    d(new THttpRequestData)
{
}

/*!
  \fn THttpRequest::THttpRequest(const THttpRequest &other)
  Copy constructor.
*/
THttpRequest::THttpRequest(const THttpRequest &other) :
    d(other.d)
{
}

/*!
  Constructor with the header \a header and the body \a body.
*/
THttpRequest::THttpRequest(const THttpRequestHeader &header, const QByteArray &body, const QHostAddress &clientAddress) :
    d(new THttpRequestData)
{
    d->header = header;
    d->bodyArray = body;
    d->clientAddress = clientAddress;
    parseBody(body, header);
}

/*!
  Constructor with the header \a header and a body generated by
  reading the file \a filePath.
*/
THttpRequest::THttpRequest(const QByteArray &header, const QString &filePath, const QHostAddress &clientAddress) :
    d(new THttpRequestData)
{
    d->header = THttpRequestHeader(header);
    d->clientAddress = clientAddress;
    d->multipartFormData = TMultipartFormData(filePath, boundary());
    d->formItems = d->multipartFormData.postParameters;
}

/*!
  Destructor.
*/
THttpRequest::~THttpRequest()
{
    if (bodyDevice) {
        bodyDevice->close();
        delete bodyDevice;
    }
}

/*!
  Assignment operator.
*/
THttpRequest &THttpRequest::operator=(const THttpRequest &other)
{
    if (bodyDevice) {
        bodyDevice->close();
        delete bodyDevice;
        bodyDevice = nullptr;
    }

    d = other.d;
    return *this;
}

/*!
  Returns the method of an HTTP request, which can be overridden by
  another value, a query parameter named '_method' or
  X-HTTP-Method-Override header, etc.
  @sa THttpRequest::realMethod()
  @sa EnableHttpMethodOverride of application.ini
 */
Tf::HttpMethod THttpRequest::method() const
{
    Tf::HttpMethod method = Tf::Invalid;
    if (httpMethodOverride()) {
        method = queryItemMethod();  // query parameter named '_method'
        if (method == Tf::Invalid) {
            method = getHttpMethodOverride();  // X-HTTP-* methods override
        }
    }

    if (method == Tf::Invalid) {
        method = realMethod();
    }
    return method;
}

/*!
  Returns the real method of an HTTP request.
  @sa THttpRequest::method()
 */
Tf::HttpMethod THttpRequest::realMethod() const
{
    QString s = d->header.method().toLower();
    return methodHash()->value(s, Tf::Invalid);
}

/*!
  Returns a method value of X-HTTP methods override for REST API.
*/
Tf::HttpMethod THttpRequest::getHttpMethodOverride() const
{
    Tf::HttpMethod method;
    QString str = d->header.rawHeader(QByteArrayLiteral("X-HTTP-Method-Override")).toLower();
    method = methodHash()->value(str, Tf::Invalid);
    if (method != Tf::Invalid) {
        return method;
    }

    str = d->header.rawHeader(QByteArrayLiteral("X-HTTP-Method")).toLower();
    method = methodHash()->value(str, Tf::Invalid);
    if (method != Tf::Invalid) {
        return method;
    }

    str = d->header.rawHeader(QByteArrayLiteral("X-METHOD-OVERRIDE")).toLower();
    method = methodHash()->value(str, Tf::Invalid);
    return method;
}

/*!
  Returns a method value as a query parameter named '_method'
  for REST API.
*/
Tf::HttpMethod THttpRequest::queryItemMethod() const
{
    QString queryMethod = queryItemValue(QStringLiteral("_method"));
    return methodHash()->value(queryMethod, Tf::Invalid);
}


/*!
  Returns the string value whose name is equal to \a name from the URL or the
  form data.
 */
QString THttpRequest::parameter(const QString &name) const
{
    return allParameters()[name].toString();
}


bool THttpRequest::hasItem(const QString &name, const QList<QPair<QString, QString>> &items)
{
    const QRegExp rx(QRegExp::escape(name) + QLatin1String("(\\[[^\\[\\]]*\\]){0,2}"));

    for (auto &p : items) {
        if (rx.exactMatch(p.first)) {
            return true;
        }
    }
    return false;
}

/*!
  Returns true if there is a query string pair whose name is equal to \a name
  from the URL.
 */
bool THttpRequest::hasQueryItem(const QString &name) const
{
    return hasItem(name, d->queryItems);
}

/*!
  Returns the query string value whose name is equal to \a name from the URL.
 */
QString THttpRequest::queryItemValue(const QString &name) const
{
    return queryItemValue(name, QString());
}


QString THttpRequest::itemValue(const QString &name, const QString &defaultValue, const QList<QPair<QString, QString>> &items)
{
    for (auto &p : items) {
        if (p.first == name) {
            return p.second;
        }
    }
    return defaultValue;
}

/*!
  This is an overloaded function.
  Returns the query string value whose name is equal to \a name from the URL.
  If the query string contains no item with the given \a name, the function
  returns \a defaultValue.
 */
QString THttpRequest::queryItemValue(const QString &name, const QString &defaultValue) const
{
    return itemValue(name, defaultValue, d->queryItems);
}


QStringList THttpRequest::allItemValues(const QString &name, const QList<QPair<QString, QString>> &items)
{
    QStringList ret;
    for (auto &p : items) {
        if (p.first == name) {
            ret << p.second;
        }
    }
    return ret;
}

/*!
  Returns the list of query string values whose name is equal to \a name from
  the URL.
  \see QStringList queryItemList()
 */
QStringList THttpRequest::allQueryItemValues(const QString &name) const
{
    return allItemValues(name, d->queryItems);
}

/*!
  Returns the list of query string value whose key is equal to \a key, such as
  "foo[]", from the URL.
  \see QStringList THttpRequest::allQueryItemValues()
 */
QStringList THttpRequest::queryItemList(const QString &key) const
{
    QString k = key;
    if (!k.endsWith(QLatin1String("[]"))) {
        k += QLatin1String("[]");
    }
    return allQueryItemValues(k);
}

/*!
  Returns the list of query value whose key is equal to \a key, such as
  "foo[]", from the URL.
 */
QVariantList THttpRequest::queryItemVariantList(const QString &key) const
{
    return itemVariantList(key, d->queryItems);
}

/*!
  Returns the map of query value whose key is equal to \a key from
  the URL.
 */
QVariantMap THttpRequest::queryItems(const QString &key) const
{
    return itemMap(key, d->queryItems);
}


QVariantMap THttpRequest::itemMap(const QList<QPair<QString, QString>> &items)
{
    QVariantMap map;
    for (auto &p : items) {
        map.insertMulti(p.first, p.second);
    }
    return map;
}

/*!
  \fn QVariantMap THttpRequest::queryItems() const
  Returns the query string of the URL, as a map of keys and values.
 */
QVariantMap THttpRequest::queryItems() const
{
    return itemMap(d->queryItems);
}

/*!
  \fn bool THttpRequest::hasForm() const

  Returns true if the request contains form data.
 */


/*!
  Returns true if there is a string pair whose name is equal to \a name from
  the form data.
 */
bool THttpRequest::hasFormItem(const QString &name) const
{
    return hasItem(name, d->formItems);
}

/*!
  Returns the string value whose name is equal to \a name from the form data.
 */
QString THttpRequest::formItemValue(const QString &name) const
{
    return formItemValue(name, QString());
}

/*!
  This is an overloaded function.
  Returns the string value whose name is equal to \a name from the form data.
  If the form data contains no item with the given \a name, the function
  returns \a defaultValue.
 */
QString THttpRequest::formItemValue(const QString &name, const QString &defaultValue) const
{
    return itemValue(name, defaultValue, d->formItems);
}

/*!
  Returns the list of string value whose name is equal to \a name from the
  form data.
  \see QStringList formItemList()
 */
QStringList THttpRequest::allFormItemValues(const QString &name) const
{
    return allItemValues(name, d->formItems);
}

/*!
  Returns the list of string value whose key is equal to \a key, such as
  "foo[]", from the form data.
  \see QStringList allFormItemValues()
 */
QStringList THttpRequest::formItemList(const QString &key) const
{
    QString k = key;
    if (!k.endsWith(QLatin1String("[]"))) {
        k += QLatin1String("[]");
    }
    return allFormItemValues(k);
}


QVariantList THttpRequest::itemVariantList(const QString &key, const QList<QPair<QString, QString>> &items)
{
    // format of key: hoge[][foo] or hoge[][]
    QVariantList lst;
    const QRegExp rx(QRegExp::escape(key) + QLatin1String("\\[\\]\\[([^\\[\\]]*)\\]"));

    for (auto &p : items) {
        if (rx.exactMatch(p.first)) {
            QString k = rx.cap(1);
            if (k.isEmpty()) {
                lst << p.second;
            } else {
                lst << QVariantMap({{k, p.second}});
            }
        }
    }
    return lst;
}

/*!
  Returns the list of QVariant value whose key is equal to \a key, such as
  "foo[]", from the form data.
 */
QVariantList THttpRequest::formItemVariantList(const QString &key) const
{
    return itemVariantList(key, d->formItems);
}


QVariantMap THttpRequest::itemMap(const QString &key, const QList<QPair<QString, QString>> &items)
{
    // format of key: hoge[foo], hoge[foo][] or hoge[foo][fuga]
    QVariantMap map;
    const QRegExp rx(QRegExp::escape(key) + QLatin1String("\\[([^\\[\\]]+)\\]"));
    const QRegExp rx2(QRegExp::escape(key) + QLatin1String("\\[([^\\[\\]]+)\\]\\[([^\\[\\]]*)\\]"));

    for (auto &p : items) {
        if (rx.exactMatch(p.first)) {
            map.insert(rx.cap(1), p.second);

        } else if (rx2.exactMatch(p.first)) {
            QString k1 = rx2.cap(1);
            QString k2 = rx2.cap(2);
            QVariant v = map.value(k1);

            if (k2.isEmpty()) {
                // QMap<QString, QVariantList>
                auto vlst = v.toList();
                vlst << p.second;
                map.insert(k1, QVariant(vlst));
            } else {
                // QMap<QString, QVariantMap>
                auto vmap = v.toMap();
                vmap.insertMulti(k2, p.second);
                map.insert(k1, QVariant(vmap));
            }
        } else {
            // do nothing
        }
    }
    return map;
}

/*!
  Returns the map of variant value whose key is equal to \a key from
  the form data.
 */
QVariantMap THttpRequest::formItems(const QString &key) const
{
    return itemMap(key, d->formItems);
}

/*!
  Returns the map of all form data.
*/
QVariantMap THttpRequest::formItems() const
{
    return itemMap(d->formItems);
}


void THttpRequest::parseBody(const QByteArray &body, const THttpRequestHeader &header)
{
    switch (method()) {
    case Tf::Post:
    case Tf::Put:
    case Tf::Patch: {
        QString ctype = QString::fromLatin1(header.contentType().trimmed());
        if (ctype.startsWith(QLatin1String("multipart/form-data"), Qt::CaseInsensitive)) {
            // multipart/form-data
            d->multipartFormData = TMultipartFormData(body, boundary());
            d->formItems = d->multipartFormData.postParameters;

        } else if (ctype.startsWith(QLatin1String("application/json"), Qt::CaseInsensitive)) {
            QJsonParseError error;
            d->jsonData = QJsonDocument::fromJson(body, &error);
            if (error.error != QJsonParseError::NoError) {
                tSystemWarn("Json data: %s\n error: %s\n at: %d", body.data(), qPrintable(error.errorString()),
                    error.offset);
            }
        } else if (ctype.startsWith(QLatin1String("application/x-www-form-urlencoded"), Qt::CaseInsensitive)) {
            if (!body.isEmpty()) {
                d->formItems = THttpUtility::fromFormUrlEncoded(body);
            }
        } else {
            tSystemWarn("unsupported content-type: %s", qPrintable(ctype));
        }
    } /* FALLTHRU */

    case Tf::Get: {
        // query parameter
        QByteArrayList data = header.path().split('?');
        QString query = QString::fromLatin1(data.value(1));

        if (!query.isEmpty()) {
            d->queryItems = THttpRequest::fromQuery(query);
        }
        break;
    }

    default:
        // do nothing
        break;
    }
}


QList<QPair<QString, QString>> THttpRequest::fromQuery(const QString &query)
{
    return THttpUtility::fromFormUrlEncoded(query.toLatin1());
}


/*!
  Returns the boundary of multipart/form-data.
*/
QByteArray THttpRequest::boundary() const
{
    QByteArray boundary;
    QString contentType = d->header.rawHeader(QByteArrayLiteral("content-type")).trimmed();

    if (contentType.startsWith(QLatin1String("multipart/form-data"), Qt::CaseInsensitive)) {
        const QStringList lst = contentType.split(QChar(';'), QString::SkipEmptyParts, Qt::CaseSensitive);
        for (auto &bnd : lst) {
            QString string = bnd.trimmed();
            if (string.startsWith(QLatin1String("boundary="), Qt::CaseInsensitive)) {
                boundary = string.mid(9).toLatin1();
                // strip optional surrounding quotes (RFC 2046 and 7578)
                if (boundary.startsWith('"') && boundary.endsWith('"')) {
                    boundary = boundary.mid(1, boundary.size() - 2);
                }
                boundary.prepend("--");
                break;
            }
        }
    }
    return boundary;
}

/*!
  Returns the cookie associated with the name.
 */
QByteArray THttpRequest::cookie(const QString &name) const
{
    return d->header.cookie(name);
}

/*!
  Returns the all cookies.
 */
QList<TCookie> THttpRequest::cookies() const
{
    return d->header.cookies();
}

/*!
  Returns a map of all form data.
*/
QVariantMap THttpRequest::allParameters() const
{
    auto params = d->queryItems;
    params << d->formItems;
    return itemMap(params);
}


QList<THttpRequest> THttpRequest::generate(const QByteArray &byteArray, const QHostAddress &address)
{
    QList<THttpRequest> reqList;
    int from = 0;
    int headidx;

    while ((headidx = byteArray.indexOf(Tf::CRLFCRLF, from)) > 0) {
        headidx += 4;
        THttpRequestHeader header(byteArray.mid(from));

        int contlen = header.contentLength();
        if (contlen <= 0) {
            reqList << THttpRequest(header, QByteArray(), address);
        } else {
            reqList << THttpRequest(header, byteArray.mid(headidx, contlen), address);
        }
        from = headidx + contlen;
    }

    return reqList;
}


QHostAddress THttpRequest::originatingClientAddress() const
{
    static const bool EnableForwardedForHeader = Tf::appSettings()->value(Tf::EnableForwardedForHeader, false).toBool();

    QString remoteHost;
    if (EnableForwardedForHeader) {
        remoteHost = QString::fromLatin1(header().rawHeader(QByteArrayLiteral("X-Forwarded-For")));
        remoteHost = remoteHost.split(QChar(',')).value(0).trimmed();
    }

    return (remoteHost.isEmpty()) ? clientAddress() : QHostAddress(remoteHost);
}


QIODevice *THttpRequest::rawBody()
{
    if (!bodyDevice) {
        if (!d->multipartFormData.bodyFile.isEmpty()) {
            bodyDevice = new QFile(d->multipartFormData.bodyFile);
        } else {
            bodyDevice = new QBuffer(&d->bodyArray);
        }
    }
    return bodyDevice;
}


/*!
  \fn const THttpRequestHeader &THttpRequest::header() const
  Returns the HTTP header of the request.
*/

/*!
  \fn TMultipartFormData &THttpRequest::multipartFormData()
  Returns a object of multipart/form-data.
*/

/*!
  \fn QHostAddress THttpRequest::clientAddress() const
  Returns the address of the client host.
*/

/*!
  \fn bool THttpRequest::hasJson() const
  Returns true if the request contains JSON data.
*/

/*!
  \fn const QJsonDocument &THttpRequest::jsonData() const
  Return the JSON data contained in the request.
*/

/*!
  \fn bool THttpRequest::hasQuery() const
  Returns true if the URL contains a Query.
 */
