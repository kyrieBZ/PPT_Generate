#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "app_config.h"
#include "http/http_types.h"
#include "http/router.h"
#include "utils/thread_pool.h"

class HttpServer {
 public:
  HttpServer(const ServerConfig& config, Router& router);
  ~HttpServer();

  void Start();
  void Stop();

 private:
  void AcceptLoop();
  void HandleClient(int client_fd);
  bool ParseRequest(int client_fd, HttpRequest& request);
  void SendResponse(int client_fd, const HttpResponse& response);

  ServerConfig config_;
  Router& router_;
  std::unique_ptr<ThreadPool> thread_pool_;
  std::atomic<bool> running_{false};
  int server_fd_ = -1;
  std::thread accept_thread_;
};
