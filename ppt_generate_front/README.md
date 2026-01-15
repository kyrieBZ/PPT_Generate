# PPT智能生成系统前端

基于Vue 3和Vite构建的PPT智能生成系统前端项目，与C++后端配合实现用户注册、登录和PPT生成功能。

## Recommended IDE Setup

[VS Code](https://code.visualstudio.com/) + [Vue (Official)](https://marketplace.visualstudio.com/items?itemName=Vue.volar) (and disable Vetur).

## Recommended Browser Setup

- Chromium-based browsers (Chrome, Edge, Brave, etc.):
  - [Vue.js devtools](https://chromewebstore.google.com/detail/vuejs-devtools/nhdogjmejiglipccpnnnanhbledajbpd)
  - [Turn on Custom Object Formatter in Chrome DevTools](http://bit.ly/object-formatters)
- Firefox:
  - [Vue.js devtools](https://addons.mozilla.org/en-US/firefox/addon/vue-js-devtools/)
  - [Turn on Custom Object Formatter in Firefox DevTools](https://fxdx.dev/firefox-devtools-custom-object-formatters/)

## Customize configuration

See [Vite Configuration Reference](https://vite.dev/config/).

## Project Setup

```sh
npm install
```

### Compile and Hot-Reload for Development

```sh
npm run dev
```

### Compile and Minify for Production

```sh
npm run build
```

### Lint with [ESLint](https://eslint.org/)

```sh
npm run lint
```

## 与C++后端配合使用

本项目需要与C++后端(ppt_generate_back)配合使用，后端提供RESTful API接口。

### 后端API配置

1. 开发环境API地址已配置为 `http://localhost:8080/api`
2. 生产环境API地址配置为 `/api`

### 主要API接口

- `POST /auth/login` - 用户登录
- `POST /auth/register` - 用户注册
- `POST /auth/logout` - 用户登出
- `GET /auth/user` - 获取用户信息

### 环境变量配置

开发环境配置文件 `.env.development`:
```
VITE_APP_ENV=development
VITE_APP_TITLE=PPT智能生成系统
VITE_API_URL=http://localhost:8080/api
VITE_APP_VERSION=1.0.0
```

生产环境配置文件 `.env.production`:
```
VITE_APP_ENV=production
VITE_APP_TITLE=PPT智能生成系统
VITE_API_URL=/api
VITE_APP_VERSION=1.0.0
```

### 启动顺序

1. 首先启动C++后端服务(ppt_generate_back)，监听8080端口
2. 然后启动前端开发服务器：
   ```sh
   npm run dev
   ```

### 注意事项

- 确保MySQL数据库已正确配置并启动
- 后端服务需要在8080端口运行
- 前端开发服务器默认运行在3000端口
- 前端通过代理将API请求转发到后端服务器

