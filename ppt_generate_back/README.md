# ppt_generate_back

C++17 REST backend that complements the `ppt_generate_front` Vue application. It provides authentication APIs, PPT generation metadata endpoints, and a simple MySQL-backed persistence layer that uses a reusable connection pool.

## Features

- Minimal HTTP server (single binary, no external runtime) with CORS enabled for the Vue dev server.
- REST endpoints under `/api` for login, registration, logout, profile lookup, PPT generation history, and a stubbed generation action.
- MySQL data access implemented through a thread-safe connection pool.
- Password hashing with salted SHA-256 plus randomly generated bearer tokens stored in the database.
- Config-driven deployment (JSON file) so you can adapt ports, credentials, and pool sizes without recompiling.

## Project layout

```
ppt_generate_back/
├── CMakeLists.txt
├── config
│   └── config.example.json
├── include
│   ├── app_config.h
│   ├── controllers/
│   ├── database/
│   ├── http/
│   ├── models/
│   ├── services/
│   └── utils/
├── sql
│   └── schema.sql
└── src
    ├── main.cpp
    ├── controllers/
    ├── database/
    ├── http/
    ├── services/
    └── utils/
```

## Build prerequisites

- g++ 11+ (C++17 is required)
- CMake 3.18+
- MySQL client dev package (`libmysqlclient-dev`)
- OpenSSL dev package (`libssl-dev`)
- nlohmann-json (`nlohmann-json3-dev`)

The dev container/environment already includes these packages.

## Configure database

1. Create a MySQL schema (defaults assume `ppt_generate`).
2. Run the SQL migration:
   ```sh
   mysql -u <user> -p ppt_generate < sql/schema.sql
   ```
3. Create an application user with minimal privileges (SELECT/INSERT/UPDATE/DELETE on the schema).

## Application config

Copy the sample file and edit credentials:

```sh
cp config/config.example.json config/config.json
```

Key fields:
- `server.host` plus `server.port` (default 8080 to match the frontend proxy).
- `database` section for connection info and pool size.
- `auth.token_ttl_minutes` to adjust bearer token lifetime.
- `providers.qwen_api_key` 设置为通义千问的 DashScope API Key，可启用真实文本生成；留空则退回到占位内容。

## Build & run

```sh
cmake -S . -B build
cmake --build build -j
./build/ppt_generate_back --config config/config.json
```

The binary listens on the configured host/port (8080 by default).

## REST API

All routes are prefixed with `/api` and respond with JSON.

| Method | Path               | Description                                               |
|--------|-------------------|-----------------------------------------------------------|
| POST   | `/auth/register`  | Register a new user, returns token+user.                  |
| POST   | `/auth/login`     | Login with username or email.                             |
| POST   | `/auth/logout`    | Invalidate the current token.                             |
| GET    | `/auth/user`      | Return profile info for current token.                    |
| POST   | `/ppt/generate`   | Persist a PPT generation request (stub).                  |
| GET    | `/ppt/history`    | List generation history for the user.                     |
| GET    | `/templates`      | Return curated PPT templates from free provider websites. |
| GET    | `/models`         | Available PPT generation models / providers.              |
| GET    | `/health`         | Basic liveness check.                                     |

Authentication: send `Authorization: Bearer <token>` for protected endpoints.

## Development tips

- Run the backend first (`8080`), then start the Vue dev server so proxying works.
- Logs print in plain text with timestamps; adjust verbosity in `logger.h` if needed.
- The HTTP server is intentionally simple—extend `Router` and add controllers/services as needed.
