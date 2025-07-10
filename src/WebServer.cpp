// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#include "ESPAsyncWebServer.h"
#include "WebHandlerImpl.h"

#if defined(ESP32) || defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(LIBRETINY)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif

using namespace asyncsrv;
AsyncConsole AsyncWebServerConsole;

bool ON_STA_FILTER(AsyncWebServerRequest *request) {
#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED || LT_ARD_HAS_WIFI
  return WiFi.localIP() == request->client()->localIP();
#else
  return false;
#endif
}

bool ON_AP_FILTER(AsyncWebServerRequest *request) {
#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED || LT_ARD_HAS_WIFI
  return WiFi.localIP() != request->client()->localIP();
#else
  return false;
#endif
}

#ifndef HAVE_FS_FILE_OPEN_MODE
const char *fs::FileOpenMode::read = "r";
const char *fs::FileOpenMode::write = "w";
const char *fs::FileOpenMode::append = "a";
#endif

AsyncWebServer::AsyncWebServer(uint16_t port) : _server(port) {
  _catchAllHandler = new AsyncCallbackWebHandler();
  _server.onClient(
    [](void *s, AsyncClient *c) {
      if (c == NULL) {
        return;
      }
      c->setRxTimeout(SERVER_RX_TIMEOUT);
      AsyncWebServerRequest *r = new (std::nothrow) AsyncWebServerRequest((AsyncWebServer *)s, c);
      ASYNC_SERVER_CONSOLE_I("s %u, c %u, r %u", s, c, r);
      if (r == NULL) {
        c->abort();
#if !defined(ASYNC_TCP_DELETION_HANDLE) || (ASYNC_TCP_DELETION_HANDLE == 0)
        delete c;
#endif
      }
    },
    this
  );
}

AsyncWebServer::~AsyncWebServer() {
  reset();
  end();
  delete _catchAllHandler;
  _catchAllHandler = nullptr;  // Prevent potential use-after-free
}

AsyncWebRewrite &AsyncWebServer::addRewrite(std::shared_ptr<AsyncWebRewrite> rewrite) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_rewriteLock);
#endif
  _rewrites.emplace_back(rewrite);
  return *_rewrites.back().get();
}

AsyncWebRewrite &AsyncWebServer::addRewrite(AsyncWebRewrite *rewrite) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_rewriteLock);
#endif
  _rewrites.emplace_back(rewrite);
  return *_rewrites.back().get();
}

bool AsyncWebServer::removeRewrite(AsyncWebRewrite *rewrite) {
  return removeRewrite(rewrite->from().c_str(), rewrite->toUrl().c_str());
}

bool AsyncWebServer::removeRewrite(const char *from, const char *to) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_rewriteLock);
#endif
  for (auto r = _rewrites.begin(); r != _rewrites.end(); ++r) {
    if (r->get()->from() == from && r->get()->toUrl() == to) {
      _rewrites.erase(r);
      return true;
    }
  }
  return false;
}

AsyncWebRewrite &AsyncWebServer::rewrite(const char *from, const char *to) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_rewriteLock);
#endif
  _rewrites.emplace_back(std::make_shared<AsyncWebRewrite>(from, to));
  return *_rewrites.back().get();
}

AsyncWebHandler &AsyncWebServer::addHandler(AsyncWebHandler *handler) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_handleLock);
#endif
  _handlers.emplace_back(handler);
  return *(_handlers.back().get());
}

bool AsyncWebServer::removeHandler(AsyncWebHandler *handler) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_handleLock);
#endif
  for (auto i = _handlers.begin(); i != _handlers.end(); ++i) {
    if (i->get() == handler) {
      _handlers.erase(i);
      return true;
    }
  }
  return false;
}

bool AsyncWebServer::begin(uint16_t port) {
  _server.setNoDelay(true);
  return _server.begin(port);
}

void AsyncWebServer::end() {
  _server.end();
}

#if ASYNC_TCP_SSL_ENABLED
void AsyncWebServer::onSslFileRequest(AcSSlFileHandler cb, void *arg) {
  _server.onSslFileRequest(cb, arg);
}

void AsyncWebServer::beginSecure(const char *cert, const char *key, const char *password) {
  _server.beginSecure(cert, key, password);
}
#endif

void AsyncWebServer::_handleDisconnect(AsyncWebServerRequest *request) {
  ASYNC_SERVER_CONSOLE_I("Del r %u", request);
  delete request;
}

void AsyncWebServer::_rewriteRequest(AsyncWebServerRequest *request) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_rewriteLock);
#endif
  // the last rewrite that matches the request will be used
  // we do not break the loop to allow for multiple rewrites to be applied and only the last one to be used (allows overriding)
  for (const auto &r : _rewrites) {
    if (r->match(request)) {
      request->_url = r->toUrl();
      request->_addGetParams(r->params());
    }
  }
}

void AsyncWebServer::_attachHandler(AsyncWebServerRequest *request) {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_handleLock);
#endif
  for (auto &h : _handlers) {
    if (h->filter(request) && h->canHandle(request)) {
      request->setHandler(h.get());
      return;
    }
  }
  // ESP_LOGD("AsyncWebServer", "No handler found for %s, using _catchAllHandler pointer: %p", request->url().c_str(), _catchAllHandler);
  request->setHandler(_catchAllHandler);
}

AsyncCallbackWebHandler &AsyncWebServer::on(
  const char *uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload, ArBodyHandlerFunction onBody
) {
  AsyncCallbackWebHandler *handler = new AsyncCallbackWebHandler();
  handler->setUri(uri);
  handler->setMethod(method);
  handler->onRequest(onRequest);
  handler->onUpload(onUpload);
  handler->onBody(onBody);
  addHandler(handler);
  return *handler;
}

AsyncStaticWebHandler &AsyncWebServer::serveStatic(const char *uri, fs::FS &fs, const char *path, const char *cache_control) {
  AsyncStaticWebHandler *handler = new AsyncStaticWebHandler(uri, fs, path, cache_control);
  addHandler(handler);
  return *handler;
}

void AsyncWebServer::onNotFound(ArRequestHandlerFunction fn) {
  _catchAllHandler->onRequest(fn);
}

void AsyncWebServer::onFileUpload(ArUploadHandlerFunction fn) {
  _catchAllHandler->onUpload(fn);
}

void AsyncWebServer::onRequestBody(ArBodyHandlerFunction fn) {
  _catchAllHandler->onBody(fn);
}

AsyncWebHandler &AsyncWebServer::catchAllHandler() const {
  return *_catchAllHandler;
}

void AsyncWebServer::reset() {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock1(_rewriteLock);
  std::lock_guard<std::recursive_mutex> lock2(_handleLock);
#endif
  _rewrites.clear();
  _handlers.clear();

  _catchAllHandler->onRequest(NULL);
  _catchAllHandler->onUpload(NULL);
  _catchAllHandler->onBody(NULL);
}
