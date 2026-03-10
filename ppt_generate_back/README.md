# ppt_generate_back

C++17 REST backend that complements the `ppt_generate_front` Vue application. It provides authentication APIs, PPT generation metadata endpoints, and a simple MySQL-backed persistence layer that uses a reusable connection pool.

## Features

- Minimal HTTP server (single binary, no external runtime) with CORS enabled for the Vue dev server.
- REST endpoints under `/api` for login, registration, logout, profile lookup, PPT generation history, and a stubbed generation action.
- MySQL data access implemented through a thread-safe connection pool.
- Password hashing with salted SHA-256 plus randomly generated bearer tokens stored in the database.
- Config-driven deployment (JSON file) so you can adapt ports, credentials, and pool sizes without recompiling.

## PPT generation mode (builder_mode)

The backend can generate PPTX in two ways (see `config/generation`):

- **`builder_mode: "python"`** (default): Uses the Python script and a PPT template (python-pptx). Requires `python_binary` and `builder_script`, and a valid template file.
- **`builder_mode: "pptxgenjs"`**: Uses the Node.js PptxGenJS script to generate slides from scratch (no template). Requires Node.js, `node_binary`, and `pptxgen_builder_script`. Run the backend from the `ppt_generate_back` directory so `node_modules/pptxgenjs` is found.

To use PptxGenJS: set `"builder_mode": "pptxgenjs"` in `config/config.json`, run `npm install` in `ppt_generate_back`, and start the server with working directory `ppt_generate_back`.

## Project layout

```
ppt_generate_back/
├── CMakeLists.txt
├── package.json
├── scripts
│   ├── libreoffice_ppt_builder.py
│   └── pptxgen_builder.js
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
- `database` section for connection info, pool size, and optional `query_timeout_seconds` (default 30) for MySQL read/write timeout.
- `auth.token_ttl_minutes` to adjust bearer token lifetime.
- `providers.qwen_api_key` 设置为通义千问的 DashScope API Key，可启用真实文本生成；留空则退回到占位内容。
- `providers.qwen_timeout_seconds`（默认 60）、`providers.doubao_timeout_seconds`（默认 30）用于外部 API 请求超时。

## Build & run

```sh
cd build
cmake ..
make -j$(nproc)
./ppt_generate_back --config ../config/config.json
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
| GET    | `/metrics`        | Process metrics (generation total/success/failed, last_duration_ms). |

Authentication: send `Authorization: Bearer <token>` for protected endpoints.

## Development tips

- Run the backend first (`8080`), then start the Vue dev server so proxying works.
- Logs print in plain text with timestamps; adjust verbosity in `logger.h` if needed.
- The HTTP server is intentionally simple—extend `Router` and add controllers/services as needed.
