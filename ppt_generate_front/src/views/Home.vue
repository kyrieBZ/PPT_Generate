<template>
  <div class="home-shell">
    <div class="bg-orb orb-a"></div>
    <div class="bg-orb orb-b"></div>
    <div class="bg-grid"></div>
    <header class="home-nav">
      <div class="brand">
        <div class="brand-logo">
          <img src="/icons/bz-logo.svg" alt="BZ logo" />
        </div>
        <div>
          <p class="brand-title">个人主页</p>
          <p class="brand-subtitle">Developer · Builder · Dreamer</p>
        </div>
      </div>
      <nav class="nav-links">
        <router-link class="nav-link active" to="/login">PPT自动生成</router-link>
        <button class="nav-link disabled" disabled>WebServer</button>
        <button class="nav-link disabled" disabled>斗地主</button>
        <button class="nav-link disabled" disabled>云存储</button>
      </nav>
    </header>

    <main class="home-main">
      <section class="hero">
        <div class="hero-content">
          <div class="hero-logo">
            <img src="/icons/bz-logo.svg" alt="BZ logo" />
          </div>
          <div class="hero-text">
            <h1>你好，我是 BZ</h1>
            <p>
              专注于将 AI 与产品体验结合，打造高效实用的应用。这里是我的个人主页，
              你可以快速访问我正在建设的项目与联系方式。
            </p>
            <div class="hero-actions">
              <router-link class="primary-btn" to="/login">进入 PPT 自动生成</router-link>
              <button class="ghost-btn" disabled>其他项目即将开放</button>
            </div>
          </div>
        </div>
        <div class="hero-panel">
          <div class="panel-card">
            <h3>正在进行</h3>
            <p>以生成式 AI 为核心的 PPT 自动生成平台。</p>
            <div class="panel-tags">
              <span>GenAI</span>
              <span>Design System</span>
              <span>Product</span>
            </div>
          </div>
          <div class="panel-card thin">
            <h3>开发关键词</h3>
            <p>前后端全栈 · 数据可视化 · AI 工作流</p>
            <div class="panel-tags">
              <span>Vue 3</span>
              <span>C++</span>
              <span>ECharts</span>
            </div>
          </div>
        </div>
      </section>

      <section class="projects">
        <h2>项目导航</h2>
        <div class="project-grid">
          <div class="project-card active">
            <h3>PPT 自动生成</h3>
            <p>内容生成、模板匹配与一键导出。</p>
            <router-link class="card-link" to="/login">进入项目</router-link>
          </div>
          <div class="project-card disabled">
            <h3>WebServer</h3>
            <p>高并发服务探索中，即将上线。</p>
            <span class="card-link disabled">暂不可用</span>
          </div>
          <div class="project-card disabled">
            <h3>斗地主</h3>
            <p>休闲娱乐项目，开发中。</p>
            <span class="card-link disabled">暂不可用</span>
          </div>
          <div class="project-card disabled">
            <h3>云存储</h3>
            <p>个人云盘与文件协作规划中。</p>
            <span class="card-link disabled">暂不可用</span>
          </div>
        </div>
      </section>
    </main>

    <footer class="home-footer">
      <div class="footer-title">
        <h2>联系我</h2>
        <p>欢迎合作交流，扫码或访问主页。</p>
      </div>
      <div class="contact-grid">
        <div class="contact-card qr-card">
          <div class="icon-wrapper">
            <img src="/icons/qq.svg" alt="QQ" />
          </div>
          <div>
            <h4>QQ</h4>
            <p>悬停查看二维码</p>
          </div>
          <div class="qr-bubble">
            <img src="/qr/QQ.jpg" alt="QQ二维码" />
            <span>扫码添加好友</span>
          </div>
        </div>
        <div class="contact-card qr-card">
          <div class="icon-wrapper">
            <img src="/icons/wechat.svg" alt="微信" />
          </div>
          <div>
            <h4>微信</h4>
            <p>悬停查看二维码</p>
          </div>
          <div class="qr-bubble">
            <img src="/qr/Wechat.jpg" alt="微信二维码" />
            <span>扫码添加好友</span>
          </div>
        </div>
        <a class="contact-card link" :href="links.github" target="_blank" rel="noopener">
          <div class="icon-wrapper">
            <img src="/icons/github.svg" alt="GitHub" />
          </div>
          <div>
            <h4>GitHub</h4>
            <p>查看开源项目</p>
          </div>
        </a>
        <a class="contact-card link" :href="links.gitee" target="_blank" rel="noopener">
          <div class="icon-wrapper">
            <img src="/icons/gitee.svg" alt="Gitee" />
          </div>
          <div>
            <h4>Gitee</h4>
            <p>国内镜像与同步</p>
          </div>
        </a>
        <a class="contact-card link" :href="links.csdn" target="_blank" rel="noopener">
          <div class="icon-wrapper">
            <img src="/icons/csdn.svg" alt="CSDN" />
          </div>
          <div>
            <h4>CSDN</h4>
            <p>技术文章与分享</p>
          </div>
        </a>
      </div>
      <div class="footer-note">© 2026 BK Portfolio. All rights reserved.</div>
    </footer>
  </div>
</template>

<script setup>
const links = {
  github: 'https://github.com/kyrieBZ?tab=repositories',
  gitee: 'https://gitee.com/KyrieBZ',
  csdn: 'https://blog.csdn.net/2303_76152639?spm=1000.2115.3001.5343'
}
</script>

<style scoped>
@import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=Fraunces:opsz,wght@9..144,500;700&display=swap');

.home-shell {
  --ink: #0b1020;
  --muted: #6b7280;
  --accent: #2563eb;
  --accent-2: #22c55e;
  --accent-3: #f97316;
  --surface: rgba(255, 255, 255, 0.85);
  --glass: rgba(255, 255, 255, 0.7);
  --shadow: 0 30px 60px rgba(15, 23, 42, 0.16);
  min-height: 100vh;
  background: radial-gradient(circle at top left, rgba(37, 99, 235, 0.22), transparent 60%),
    radial-gradient(circle at 70% 80%, rgba(34, 197, 94, 0.18), transparent 55%),
    linear-gradient(120deg, #f8fafc 0%, #eef2ff 45%, #ecfeff 100%);
  font-family: 'Space Grotesk', 'Noto Sans SC', sans-serif;
  color: var(--ink);
  position: relative;
  overflow: hidden;
}

.bg-orb {
  position: absolute;
  border-radius: 999px;
  filter: blur(0px);
  opacity: 0.55;
  pointer-events: none;
  animation: float 16s ease-in-out infinite;
}

.orb-a {
  width: 280px;
  height: 280px;
  background: radial-gradient(circle at 30% 30%, rgba(37, 99, 235, 0.45), rgba(37, 99, 235, 0.05));
  top: -80px;
  left: -60px;
}

.orb-b {
  width: 360px;
  height: 360px;
  background: radial-gradient(circle at 30% 30%, rgba(34, 197, 94, 0.4), rgba(34, 197, 94, 0.05));
  bottom: -140px;
  right: -120px;
  animation-delay: -4s;
}

.bg-grid {
  position: absolute;
  inset: 0;
  background-image: radial-gradient(rgba(15, 23, 42, 0.08) 1px, transparent 0);
  background-size: 18px 18px;
  opacity: 0.35;
  pointer-events: none;
}

.home-nav {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 28px 8vw 10px;
  position: sticky;
  top: 0;
  z-index: 5;
  background: linear-gradient(180deg, rgba(248, 250, 252, 0.85) 0%, rgba(248, 250, 252, 0) 100%);
  backdrop-filter: blur(8px);
}

.brand {
  display: flex;
  align-items: center;
  gap: 12px;
}

.brand-logo {
  width: 44px;
  height: 44px;
  border-radius: 16px;
  display: grid;
  place-items: center;
  font-weight: 700;
}

.brand-logo img {
  width: 44px;
  height: 44px;
}

.brand-title {
  font-weight: 600;
  font-size: 1.1rem;
}

.brand-subtitle {
  color: var(--muted);
  font-size: 0.85rem;
}

.nav-links {
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
}

.nav-link {
  padding: 8px 16px;
  border-radius: 999px;
  border: 1px solid rgba(15, 23, 42, 0.15);
  background: var(--glass);
  font-weight: 600;
  color: var(--ink);
  cursor: pointer;
  transition: all 0.2s ease;
}

.nav-link.active {
  background: var(--ink);
  color: #f8fafc;
  box-shadow: 0 12px 30px rgba(15, 23, 42, 0.25);
}

.nav-link.disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.home-main {
  padding: 24px 8vw 60px;
  display: flex;
  flex-direction: column;
  gap: 40px;
  position: relative;
  z-index: 2;
}

.hero {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 24px;
}

.hero-content {
  display: flex;
  align-items: center;
  gap: 22px;
  background: var(--surface);
  border-radius: 24px;
  padding: 32px;
  box-shadow: var(--shadow);
  position: relative;
  overflow: hidden;
  animation: fadeUp 0.6s ease;
}

.hero-content::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 24px;
  border: 1px solid rgba(37, 99, 235, 0.15);
  pointer-events: none;
}

.hero-logo {
  width: 96px;
  height: 96px;
  border-radius: 28px;
  display: grid;
  place-items: center;
  overflow: hidden;
}

.hero-logo img {
  width: 100%;
  height: 100%;
}

.hero-text h1 {
  font-family: 'Fraunces', serif;
  font-size: 2.6rem;
  margin-bottom: 12px;
}

.hero-text p {
  color: #475569;
  line-height: 1.7;
}

.hero-actions {
  margin-top: 18px;
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.primary-btn,
.ghost-btn {
  padding: 12px 20px;
  border-radius: 12px;
  font-weight: 600;
}

.primary-btn {
  background: linear-gradient(135deg, #0f172a, #2563eb);
  color: #f8fafc;
  box-shadow: 0 12px 26px rgba(37, 99, 235, 0.3);
}

.ghost-btn {
  background: rgba(15, 23, 42, 0.06);
  border: 1px dashed rgba(15, 23, 42, 0.3);
  color: #475569;
}

.hero-panel {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.panel-card {
  background: rgba(255, 255, 255, 0.92);
  padding: 24px;
  border-radius: 20px;
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.12);
  animation: fadeUp 0.6s ease;
}

.panel-card.thin {
  background: rgba(15, 23, 42, 0.9);
  color: #e2e8f0;
}


.panel-tags {
  margin-top: 12px;
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.panel-tags span {
  padding: 4px 10px;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.08);
  font-size: 0.75rem;
}

.panel-card.thin .panel-tags span {
  background: rgba(255, 255, 255, 0.14);
}

.projects h2 {
  margin-bottom: 16px;
  font-family: 'Fraunces', serif;
}

.project-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 16px;
}

.project-card {
  padding: 20px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.9);
  border: 1px solid rgba(148, 163, 184, 0.3);
  box-shadow: 0 18px 36px rgba(15, 23, 42, 0.1);
  position: relative;
  overflow: hidden;
}

.project-card.disabled {
  opacity: 0.6;
}

.project-card.active::after {
  content: '';
  position: absolute;
  inset: 0;
  background: radial-gradient(circle at top right, rgba(37, 99, 235, 0.18), transparent 55%);
  pointer-events: none;
}

.card-link {
  display: inline-block;
  margin-top: 12px;
  font-weight: 600;
  color: #0f172a;
}

.card-link.disabled {
  color: #94a3b8;
}

.home-footer {
  padding: 40px 8vw 30px;
  background: rgba(15, 23, 42, 0.96);
  color: #e2e8f0;
  position: relative;
  z-index: 2;
  box-shadow: 0 -26px 60px rgba(15, 23, 42, 0.5),
    0 -16px 40px rgba(37, 99, 235, 0.35),
    0 -4px 0 rgba(255, 255, 255, 0.06) inset;
  overflow: hidden;
}

.home-footer::before {
  content: '';
  position: absolute;
  top: -120px;
  left: 50%;
  width: 520px;
  height: 220px;
  transform: translateX(-50%);
  background: radial-gradient(circle at center, rgba(37, 99, 235, 0.45), rgba(15, 23, 42, 0));
  pointer-events: none;
}

.home-footer::after {
  content: '';
  position: absolute;
  inset: 0;
  background: radial-gradient(circle at 20% -40%, rgba(16, 185, 129, 0.28), transparent 50%),
    radial-gradient(circle at 80% -30%, rgba(59, 130, 246, 0.25), transparent 55%);
  border-top: 1px solid rgba(255, 255, 255, 0.08);
  pointer-events: none;
}

.footer-title h2 {
  margin-bottom: 8px;
}

.footer-title {
  text-align: center;
  width: 100%;
}

.contact-grid {
  margin-top: 24px;
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 16px;
}

.contact-card {
  background: rgba(255, 255, 255, 0.08);
  border-radius: 16px;
  padding: 16px;
  display: flex;
  gap: 14px;
  align-items: center;
}

.contact-card.link {
  transition: transform 0.2s ease;
}

.contact-card.link:hover {
  transform: translateY(-4px);
}

.icon-wrapper {
  width: 72px;
  height: 72px;
  border-radius: 14px;
  background: #ffffff;
  display: grid;
  place-items: center;
}

.icon-wrapper img {
  width: 60px;
  height: 60px;
}

.qr-card {
  position: relative;
  overflow: visible;
}

.qr-bubble {
  position: absolute;
  bottom: 92px;
  left: 16px;
  background: #ffffff;
  color: #0f172a;
  border-radius: 14px;
  padding: 10px;
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.2);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  opacity: 0;
  transform: translateY(8px);
  transition: all 0.2s ease;
  pointer-events: none;
  z-index: 5;
}

.qr-bubble img {
  width: 120px;
  height: 120px;
}

.qr-bubble span {
  font-size: 0.75rem;
  color: #475569;
}

.qr-bubble::after {
  content: '';
  position: absolute;
  bottom: -8px;
  left: 24px;
  width: 16px;
  height: 16px;
  background: #ffffff;
  transform: rotate(45deg);
  box-shadow: 2px 2px 6px rgba(15, 23, 42, 0.08);
}

.qr-card:hover .qr-bubble {
  opacity: 1;
  transform: translateY(0);
}

.footer-note {
  margin-top: 24px;
  color: rgba(226, 232, 240, 0.6);
  font-size: 0.85rem;
  text-align: center;
}

@media (max-width: 1024px) {
  .hero {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 640px) {
  .home-nav {
    flex-direction: column;
    gap: 16px;
    align-items: flex-start;
  }

  .hero-content {
    flex-direction: column;
    align-items: flex-start;
  }
}

@keyframes fadeUp {
  from {
    opacity: 0;
    transform: translateY(12px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

@keyframes float {
  0%, 100% {
    transform: translateY(0px);
  }
  50% {
    transform: translateY(20px);
  }
}
</style>
