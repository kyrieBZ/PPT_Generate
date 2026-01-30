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
import { nextTick, onBeforeUnmount, onMounted } from 'vue'
import { ElMessage } from 'element-plus'

const links = {
  github: 'https://github.com/kyrieBZ?tab=repositories',
  gitee: 'https://gitee.com/KyrieBZ',
  csdn: 'https://blog.csdn.net/2303_76152639?spm=1000.2115.3001.5343'
}

let cleanupQrBubble = null

const setupQrBubbleFlip = () => {
  const cards = Array.from(document.querySelectorAll('.qr-card'))
  const viewPadding = 12
  const leftOffset = 16
  const rightOffset = 16

  const updateCard = (card) => {
    const bubble = card.querySelector('.qr-bubble')
    if (!bubble) {
      return
    }

    const cardRect = card.getBoundingClientRect()
    const bubbleWidth = bubble.offsetWidth
    const viewWidth = window.innerWidth
    const bubbleRightIfLeft = cardRect.left + leftOffset + bubbleWidth
    const bubbleLeftIfRight = cardRect.right - rightOffset - bubbleWidth
    const overflowRight = bubbleRightIfLeft > viewWidth - viewPadding
    const overflowLeft = bubbleLeftIfRight < viewPadding

    card.classList.toggle('flip-right', overflowRight && !overflowLeft)
  }

  const updateAll = () => cards.forEach(updateCard)
  const onResize = () => window.requestAnimationFrame(updateAll)

  const handlers = cards.map((card) => {
    const handler = () => updateCard(card)
    card.addEventListener('mouseenter', handler)
    card.addEventListener('focusin', handler)
    return { card, handler }
  })

  window.addEventListener('resize', onResize)
  updateAll()

  return () => {
    handlers.forEach(({ card, handler }) => {
      card.removeEventListener('mouseenter', handler)
      card.removeEventListener('focusin', handler)
    })
    window.removeEventListener('resize', onResize)
  }
}

onMounted(async () => {
  ElMessage.success('欢迎访问我的主页！')
  await nextTick()
  cleanupQrBubble = setupQrBubbleFlip()
})

onBeforeUnmount(() => {
  if (cleanupQrBubble) {
    cleanupQrBubble()
    cleanupQrBubble = null
  }
})
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
  --shadow: 0 18px 36px rgba(15, 23, 42, 0.12);
  --page-x: clamp(16px, 6vw, 96px);
  --space-2xs: clamp(6px, 0.8vw, 10px);
  --space-xs: clamp(10px, 1.2vw, 16px);
  --space-sm: clamp(14px, 1.6vw, 20px);
  --space-md: clamp(18px, 2.2vw, 28px);
  --space-lg: clamp(24px, 3vw, 40px);
  --space-xl: clamp(32px, 4vw, 56px);
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: radial-gradient(circle at top left, rgba(37, 99, 235, 0.18), transparent 60%),
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
  opacity: 0.35;
  pointer-events: none;
  animation: float 16s ease-in-out infinite;
}

.orb-a {
  width: 13.75rem;
  height: 13.75rem;
  background: radial-gradient(circle at 30% 30%, rgba(37, 99, 235, 0.35), rgba(37, 99, 235, 0.05));
  top: -5rem;
  left: -3.75rem;
}

.orb-b {
  width: 17.5rem;
  height: 17.5rem;
  background: radial-gradient(circle at 30% 30%, rgba(34, 197, 94, 0.3), rgba(34, 197, 94, 0.05));
  bottom: -8.75rem;
  right: -7.5rem;
  animation-delay: -4s;
}

.bg-grid {
  position: absolute;
  inset: 0;
  background-image: radial-gradient(rgba(15, 23, 42, 0.08) 1px, transparent 0);
  background-size: 1.375rem 1.375rem;
  opacity: 0.18;
  pointer-events: none;
}

.home-nav {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: var(--space-lg) var(--page-x) var(--space-xs);
  position: sticky;
  top: 0;
  z-index: 5;
  background: linear-gradient(180deg, rgba(248, 250, 252, 0.85) 0%, rgba(248, 250, 252, 0) 100%);
  backdrop-filter: blur(4px);
  gap: var(--space-sm);
  flex-wrap: wrap;
}

.brand {
  display: flex;
  align-items: center;
  gap: var(--space-xs);
}

.brand-logo {
  width: 2.75rem;
  height: 2.75rem;
  border-radius: 1rem;
  display: grid;
  place-items: center;
  font-weight: 700;
}

.brand-logo img {
  width: 2.75rem;
  height: 2.75rem;
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
  gap: var(--space-xs);
  flex-wrap: wrap;
}

.nav-link {
  padding: 0.5rem 1rem;
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
  padding: var(--space-md) var(--page-x) var(--space-lg);
  display: flex;
  flex-direction: column;
  gap: var(--space-lg);
  position: relative;
  z-index: 2;
  flex: 1;
}

.hero {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: var(--space-md);
  align-items: stretch;
}

.hero-content {
  display: flex;
  align-items: center;
  gap: var(--space-md);
  background: var(--surface);
  border-radius: 1.5rem;
  padding: var(--space-lg);
  box-shadow: var(--shadow);
  position: relative;
  overflow: hidden;
  animation: fadeUp 0.6s ease;
  flex-wrap: wrap;
}

.hero-content::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 1.5rem;
  border: 1px solid rgba(37, 99, 235, 0.15);
  pointer-events: none;
}

.hero-logo {
  width: 6rem;
  height: 6rem;
  border-radius: 1.75rem;
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
  font-size: clamp(1.9rem, 3vw, 2.6rem);
  margin-bottom: 0.75rem;
}

.hero-text p {
  color: #475569;
  line-height: 1.7;
}

.hero-actions {
  margin-top: 1.125rem;
  display: flex;
  gap: var(--space-xs);
  flex-wrap: wrap;
}

.primary-btn,
.ghost-btn {
  padding: 0.75rem 1.25rem;
  border-radius: 0.75rem;
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
  gap: var(--space-sm);
}

.panel-card {
  background: rgba(255, 255, 255, 0.92);
  padding: var(--space-md);
  border-radius: 1.25rem;
  box-shadow: 0 14px 28px rgba(15, 23, 42, 0.1);
  animation: fadeUp 0.6s ease;
}

.panel-card.thin {
  background: rgba(15, 23, 42, 0.9);
  color: #e2e8f0;
}


.panel-tags {
  margin-top: 0.75rem;
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.panel-tags span {
  padding: 0.25rem 0.625rem;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.08);
  font-size: 0.75rem;
}

.panel-card.thin .panel-tags span {
  background: rgba(255, 255, 255, 0.14);
}

.projects h2 {
  margin-bottom: 1rem;
  font-family: 'Fraunces', serif;
}

.project-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(13.75rem, 1fr));
  gap: var(--space-sm);
}

.project-card {
  padding: var(--space-sm);
  border-radius: 1.125rem;
  background: rgba(255, 255, 255, 0.9);
  border: 1px solid rgba(148, 163, 184, 0.3);
  box-shadow: 0 12px 24px rgba(15, 23, 42, 0.1);
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
  margin-top: 0.75rem;
  font-weight: 600;
  color: #0f172a;
}

.card-link.disabled {
  color: #94a3b8;
}

.home-footer {
  padding: var(--space-xl) var(--page-x) var(--space-lg);
  background: rgba(15, 23, 42, 0.96);
  color: #e2e8f0;
  position: relative;
  z-index: 2;
  box-shadow: none;
  overflow: visible;
}

.footer-title h2 {
  margin-bottom: 0.5rem;
}

.footer-title {
  text-align: center;
  width: 100%;
}

.contact-grid {
  margin-top: var(--space-md);
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(13.75rem, 1fr));
  gap: var(--space-sm);
}

.contact-card {
  background: rgba(255, 255, 255, 0.08);
  border-radius: 1rem;
  padding: var(--space-sm);
  display: flex;
  gap: var(--space-xs);
  align-items: center;
}

.contact-card.link {
  transition: transform 0.2s ease;
}

.contact-card.link:hover {
  transform: translateY(-4px);
}

.icon-wrapper {
  width: 4.5rem;
  height: 4.5rem;
  border-radius: 0.875rem;
  background: #ffffff;
  display: grid;
  place-items: center;
}

.icon-wrapper img {
  width: 3.75rem;
  height: 3.75rem;
}

.qr-card {
  position: relative;
  overflow: visible;
}

.qr-bubble {
  position: absolute;
  bottom: 5.75rem;
  left: 1rem;
  background: #ffffff;
  color: #0f172a;
  border-radius: 0.875rem;
  padding: 0.625rem;
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.2);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.375rem;
  opacity: 0;
  transform: translateY(8px);
  transition: all 0.2s ease;
  pointer-events: none;
  z-index: 5;
}

.qr-bubble img {
  width: 7.5rem;
  height: 7.5rem;
  object-fit: contain;
  display: block;
}

.qr-bubble span {
  font-size: 0.75rem;
  color: #475569;
}

.qr-bubble::after {
  content: '';
  position: absolute;
  bottom: -0.5rem;
  left: 1.5rem;
  width: 1rem;
  height: 1rem;
  background: #ffffff;
  transform: rotate(45deg);
  box-shadow: 2px 2px 6px rgba(15, 23, 42, 0.08);
}

.qr-card.flip-right .qr-bubble {
  left: auto;
  right: 1rem;
}

.qr-card.flip-right .qr-bubble::after {
  left: auto;
  right: 1.5rem;
}

.qr-card:hover .qr-bubble {
  opacity: 1;
  transform: translateY(0);
}

.footer-note {
  margin-top: var(--space-md);
  color: rgba(226, 232, 240, 0.6);
  font-size: 0.85rem;
  text-align: center;
}

@media (max-width: 1024px) {
  .hero {
    grid-template-columns: 1fr;
  }

  .hero-panel {
    flex-direction: row;
    flex-wrap: wrap;
  }

  .panel-card {
    flex: 1 1 240px;
  }
}

@media (max-width: 768px) {
  .home-nav {
    position: static;
    backdrop-filter: none;
  }

  .nav-links {
    width: 100%;
  }

  .nav-link {
    flex: 1 1 auto;
    text-align: center;
  }

  .hero-content {
    padding: var(--space-md);
  }

  .hero-logo {
    width: 4.5rem;
    height: 4.5rem;
    border-radius: 1.25rem;
  }

  .panel-card {
    padding: var(--space-sm);
  }

  .icon-wrapper {
    width: 3.75rem;
    height: 3.75rem;
  }

  .icon-wrapper img {
    width: 3rem;
    height: 3rem;
  }

  .bg-grid {
    display: none;
  }

  .bg-orb {
    animation: none;
  }

  .home-main {
    gap: clamp(16px, 3vw, 28px);
    padding-bottom: clamp(20px, 4vw, 36px);
  }
}

@media (max-width: 640px) {
  .home-nav {
    flex-direction: column;
    gap: var(--space-sm);
    align-items: flex-start;
  }

  .hero-content {
    flex-direction: column;
    align-items: flex-start;
  }

  .nav-links {
    width: 100%;
    justify-content: flex-start;
  }

  .nav-link {
    width: 100%;
    text-align: left;
  }
}

@media (max-width: 480px) {
  .hero-actions {
    flex-direction: column;
    width: 100%;
  }

  .primary-btn,
  .ghost-btn {
    width: 100%;
    text-align: center;
  }

  .contact-card {
    flex-direction: column;
    align-items: flex-start;
  }

  .qr-bubble {
    left: 0;
  }
}

@media (prefers-reduced-motion: reduce) {
  .bg-orb,
  .hero-content,
  .panel-card {
    animation: none;
  }

  .contact-card.link {
    transition: none;
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
