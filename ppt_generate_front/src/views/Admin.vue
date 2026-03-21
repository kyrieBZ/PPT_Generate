<template>
  <div class="admin-shell">
    <aside class="admin-sidebar" :class="{ 'admin-sidebar--collapsed': adminSidebarCollapsed }">
      <div class="brand-block">
        <div class="brand-logo">PPT OPS</div>
        <p class="brand-subtitle">后台管理控制台</p>
      </div>

      <div class="admin-user">
        <div class="admin-avatar">{{ userInitials }}</div>
        <div class="admin-user-info">
          <div class="admin-name">{{ currentUser?.username || '管理员' }}</div>
          <div class="admin-role">超级管理员</div>
        </div>
      </div>

      <nav class="admin-nav">
        <button
          v-for="item in navItems"
          :key="item.id"
          class="nav-item"
          :class="{ active: activeNav === item.id, disabled: item.disabled }"
          :disabled="item.disabled"
          @click="activeNav = item.id"
          :title="adminSidebarCollapsed ? item.label : undefined"
        >
          <span class="nav-icon">
            <el-icon><component :is="item.icon" /></el-icon>
          </span>
          <span class="nav-label">{{ item.label }}</span>
          <span v-if="item.badge" class="nav-badge">{{ item.badge }}</span>
        </button>
      </nav>

      <div class="nav-footer">
        <button class="back-btn" @click="router.push('/main')" title="返回用户端">
          <el-icon><ArrowLeft /></el-icon>
          <span class="back-btn-text">返回用户端</span>
        </button>
      </div>

      <button
        class="sidebar-toggle"
        :title="adminSidebarCollapsed ? '展开侧栏' : '收起侧栏'"
        @click="adminSidebarCollapsed = !adminSidebarCollapsed"
      >
        <el-icon>
          <component :is="adminSidebarCollapsed ? ArrowRight : ArrowLeft" />
        </el-icon>
      </button>
    </aside>

    <section class="admin-main">
      <header class="admin-topbar">
        <div>
          <h1>运营中枢 · 实时概览</h1>
          <p>集中管理生成记录、活跃度与质量指标，后续可接入大模型洞察。</p>
        </div>
        <div class="top-actions">
          <el-button class="ghost-btn" size="large" @click="refreshData">
            <el-icon><Refresh /></el-icon>
            刷新数据
          </el-button>
          <el-button type="primary" size="large" @click="exportDashboard">
            <el-icon><Download /></el-icon>
            导出概览
          </el-button>
        </div>
      </header>

      <div class="admin-content">
        <section v-show="activeNav === 'overview'" class="overview-section" v-loading="metricsLoading">
          <div class="overview-header">
            <div>
              <h2>关键运营指标</h2>
              <p>数据来源：后台统计接口（按时间区间）</p>
            </div>
            <div class="overview-meta">
              <span class="meta-chip">最近更新：{{ lastUpdated }}</span>
              <span class="meta-chip accent">数据口径：T+0</span>
            </div>
          </div>

          <div class="stats-grid">
            <div v-for="card in statCards" :key="card.id" class="stat-card">
              <div class="stat-icon">
                <el-icon><component :is="card.icon" /></el-icon>
              </div>
              <div class="stat-info">
                <p>{{ card.label }}</p>
                <h3>{{ card.value }}</h3>
                <span class="stat-footnote">{{ card.note }}</span>
              </div>
            </div>
          </div>

          <div class="overview-insights">
            <div class="insight-card">
              <h3>周期数据摘要</h3>
              <ul>
                <li>当前区间共生成 <strong>{{ totalCount }}</strong> 次请求</li>
                <li>其中成功 <strong>{{ successCount }}</strong> 次，失败 <strong>{{ failureCount }}</strong> 次</li>
                <li>涉及活跃用户 <strong>{{ uniqueUsers }}</strong> 人，覆盖模板 <strong>{{ templateCount }}</strong> 种</li>
              </ul>
            </div>
            <div class="insight-card accent">
              <h3>当前状态</h3>
              <p>当前区间成功率为 <strong>{{ successRate }}%</strong>，{{ failureCount > 0 ? `共有 ${failureCount} 条失败记录，建议关注失败原因。` : '暂无失败记录。' }}</p>
              <div class="insight-tags">
                <span>活跃用户：{{ uniqueUsers }} 人</span>
                <span>成功：{{ successCount }}</span>
                <span>失败：{{ failureCount }}</span>
              </div>
            </div>
          </div>
        </section>


        <!-- ══ 生成记录管理 ══════════════════════════════════════════════════════ -->
        <section v-show="activeNav === 'records'" class="rec-shell">

          <!-- Hero 标题栏 -->
          <div class="rec-hero">
            <div class="rec-hero__left">
              <div class="rec-hero__eyebrow">GENERATION RECORDS</div>
              <h2 class="rec-hero__title">生成记录管理</h2>
              <p class="rec-hero__sub">全量 PPT 生成历史 · 实时状态追踪 · 多维筛选导出</p>
            </div>
            <div class="rec-hero__stats">
              <div class="rec-stat-pill rec-stat-pill--all">
                <span class="rec-stat-num">{{ filteredHistory.length }}</span>
                <span class="rec-stat-label">条记录</span>
              </div>
              <div class="rec-stat-pill rec-stat-pill--ok">
                <span class="rec-stat-num">{{ filteredHistory.filter(r => r.status === 'completed').length }}</span>
                <span class="rec-stat-label">已完成</span>
              </div>
              <div class="rec-stat-pill rec-stat-pill--err">
                <span class="rec-stat-num">{{ filteredHistory.filter(r => r.status === 'failed').length }}</span>
                <span class="rec-stat-label">失败</span>
              </div>
            </div>
          </div>

          <!-- 搜索 + 筛选工具栏 -->
          <div class="rec-toolbar">
            <div class="rec-search-wrap">
              <svg class="rec-search-icon" viewBox="0 0 20 20" fill="none">
                <circle cx="8.5" cy="8.5" r="5.5" stroke="currentColor" stroke-width="1.6"/>
                <path d="M13 13l3.5 3.5" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>
              </svg>
              <input
                v-model="searchQuery"
                class="rec-search-input"
                placeholder="搜索标题 / 主题 / 用户…"
              />
              <button v-if="searchQuery" class="rec-search-clear" @click="searchQuery = ''">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14">
                  <path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>
                </svg>
              </button>
            </div>

            <div class="rec-filter-chips">
              <button
                v-for="opt in [
                  { v: 'all', label: '全部' },
                  { v: 'completed', label: '已完成' },
                  { v: 'pending', label: '进行中' },
                  { v: 'failed', label: '失败' }
                ]"
                :key="opt.v"
                class="rec-chip"
                :class="{ 'rec-chip--active': statusFilter === opt.v, [`rec-chip--${opt.v}`]: true }"
                @click="statusFilter = opt.v"
              >{{ opt.label }}</button>
            </div>

            <el-date-picker
              v-model="recordDateRange"
              type="daterange"
              range-separator="→"
              start-placeholder="开始"
              end-placeholder="结束"
              size="default"
              class="rec-datepicker"
            />

            <!-- AI 索引管理按钮组 -->
            <div class="rec-ai-index-group">
              <button
                class="rec-ai-index-btn"
                :class="{ 'rec-ai-index-btn--running': aiIndexRunning }"
                :disabled="aiIndexRunning || !aiIndexAvailable"
                @click="handleReindex"
                :title="!aiIndexAvailable ? 'Qdrant 向量服务不可用' : aiIndexRunning ? '正在重建中…' : '重建所有已完成 PPT 的 AI 向量索引'"
              >
                <svg v-if="!aiIndexRunning" viewBox="0 0 20 20" fill="none" width="15" height="15">
                  <path d="M10 2a8 8 0 1 0 5.6 13.7" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>
                  <path d="M14 10l2-4 2 4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
                  <circle cx="10" cy="10" r="2.5" fill="currentColor" opacity="0.6"/>
                </svg>
                <svg v-else class="spin-icon" viewBox="0 0 20 20" fill="none" width="15" height="15">
                  <path d="M10 2a8 8 0 1 0 8 8" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>
                </svg>
                {{ aiIndexRunning ? '索引构建中…' : '重建 AI 索引' }}
              </button>

              <!-- 索引状态气泡 -->
              <div class="rec-ai-status-pill" :class="aiIndexAvailable ? 'rec-ai-status-pill--ok' : 'rec-ai-status-pill--off'">
                <span class="rec-ai-status-dot"></span>
                <span v-if="aiIndexAvailable">
                  {{ aiIndexRunning ? '构建中' : `已索引 ${aiIndexedCount} 条` }}
                </span>
                <span v-else>向量服务离线</span>
              </div>
            </div>

            <button class="rec-export-btn" @click="exportPptHistoryCSV" title="导出 CSV">
              <svg viewBox="0 0 20 20" fill="none" width="16" height="16">
                <path d="M10 3v10M6 9l4 4 4-4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M3 15h14" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>
              </svg>
              导出 CSV
            </button>
          </div>

          <!-- 表头 -->
          <div class="rec-table-head">
            <div class="rec-col rec-col--id">ID</div>
            <div class="rec-col rec-col--title">标题 / 主题</div>
            <div class="rec-col rec-col--user">用户</div>
            <div class="rec-col rec-col--model">模型</div>
            <div class="rec-col rec-col--pages">页数</div>
            <div class="rec-col rec-col--status">状态</div>
            <div class="rec-col rec-col--time">创建时间</div>
          </div>

          <!-- 记录列表 -->
          <div class="rec-list" v-loading="adminHistoryLoading" element-loading-text="加载中…">
            <!-- 空状态 -->
            <div v-if="!adminHistoryLoading && pagedHistory.length === 0" class="rec-empty">
              <svg viewBox="0 0 64 64" fill="none" width="56" height="56" class="rec-empty-icon">
                <rect x="8" y="12" width="48" height="40" rx="6" stroke="#94a3b8" stroke-width="2"/>
                <path d="M20 24h24M20 32h16M20 40h10" stroke="#94a3b8" stroke-width="2" stroke-linecap="round"/>
              </svg>
              <p>{{ searchQuery || statusFilter !== 'all' ? '没有匹配的记录' : '暂无生成记录' }}</p>
              <span v-if="searchQuery || statusFilter !== 'all'">
                <button class="rec-reset-link" @click="searchQuery = ''; statusFilter = 'all'; recordDateRange = []">清除筛选条件</button>
              </span>
            </div>

            <!-- 记录行 -->
            <div
              v-for="row in pagedHistory"
              :key="row.id"
              class="rec-row"
              :class="`rec-row--${row.status}`"
            >
              <!-- 左侧状态条 -->
              <div class="rec-row__accent"></div>

              <div class="rec-col rec-col--id">
                <span class="rec-id-badge">#{{ row.id }}</span>
              </div>

              <div class="rec-col rec-col--title">
                <div class="rec-title-main" :title="row.title">{{ row.title || '—' }}</div>
              </div>

              <div class="rec-col rec-col--user">
                <div class="rec-avatar">{{ (row.userName || '?')[0].toUpperCase() }}</div>
                <span class="rec-username">{{ row.userName || '—' }}</span>
              </div>

              <div class="rec-col rec-col--model">
                <span class="rec-model-tag">{{ row.modelName || row.modelKey || '—' }}</span>
              </div>

              <div class="rec-col rec-col--pages">
                <span class="rec-pages">{{ row.pages || '—' }}</span>
              </div>

              <div class="rec-col rec-col--status">
                <span class="rec-status-badge" :class="`rec-status--${row.status}`">
                  <span class="rec-status-dot"></span>
                  {{ statusText(row.status) }}
                </span>
              </div>

              <div class="rec-col rec-col--time">
                <span class="rec-time">{{ formatDate(row.createdAt) }}</span>
              </div>
            </div>
          </div>

          <!-- 分页 -->
          <div class="rec-footer">
            <span class="rec-footer__count">
              显示第 {{ (currentPage - 1) * pageSize + 1 }}–{{ Math.min(currentPage * pageSize, filteredHistory.length) }} 条，共 <b>{{ filteredHistory.length }}</b> 条
            </span>
            <div class="rec-pagination">
              <button
                class="rec-page-btn"
                :disabled="currentPage <= 1"
                @click="currentPage--"
              >
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M10 4L6 8l4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
              <template v-for="p in paginationPages" :key="p">
                <span v-if="p === '…'" class="rec-page-ellipsis">…</span>
                <button
                  v-else
                  class="rec-page-btn rec-page-btn--num"
                  :class="{ 'rec-page-btn--active': p === currentPage }"
                  @click="currentPage = p"
                >{{ p }}</button>
              </template>
              <button
                class="rec-page-btn"
                :disabled="currentPage >= totalPages"
                @click="currentPage++"
              >
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M6 4l4 4-4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
            </div>
          </div>
        </section>

        <!-- ══ 用户管理 ══════════════════════════════════════════════════════════ -->
        <section v-show="activeNav === 'users'" class="mgmt-shell">

          <!-- Hero -->
          <div class="mgmt-hero mgmt-hero--emerald">
            <div class="mgmt-hero__left">
              <div class="mgmt-hero__eyebrow">USER MANAGEMENT</div>
              <h2 class="mgmt-hero__title">用户管理</h2>
              <p class="mgmt-hero__sub">账户状态管控 · 批量操作 · 权限审查</p>
            </div>
            <div class="mgmt-hero__stats">
              <div class="mgmt-stat-pill">
                <span class="mgmt-stat-num">{{ filteredUsers.length }}</span>
                <span class="mgmt-stat-label">全部用户</span>
              </div>
              <div class="mgmt-stat-pill mgmt-stat-pill--ok">
                <span class="mgmt-stat-num">{{ filteredUsers.filter(u => !u.isDisabled).length }}</span>
                <span class="mgmt-stat-label">正常</span>
              </div>
              <div class="mgmt-stat-pill mgmt-stat-pill--err">
                <span class="mgmt-stat-num">{{ filteredUsers.filter(u => u.isDisabled).length }}</span>
                <span class="mgmt-stat-label">已禁用</span>
              </div>
            </div>
          </div>

          <!-- 工具栏 -->
          <div class="mgmt-toolbar">
            <div class="rec-search-wrap">
              <svg class="rec-search-icon" viewBox="0 0 20 20" fill="none">
                <circle cx="8.5" cy="8.5" r="5.5" stroke="currentColor" stroke-width="1.6"/>
                <path d="M13 13l3.5 3.5" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>
              </svg>
              <input v-model="userSearchQuery" class="rec-search-input" placeholder="搜索用户名或邮箱…" />
              <button v-if="userSearchQuery" class="rec-search-clear" @click="userSearchQuery = ''">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14">
                  <path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>
                </svg>
              </button>
            </div>
            <div class="rec-filter-chips">
              <button v-for="opt in [{ v:'all',label:'全部' },{ v:'active',label:'正常' },{ v:'disabled',label:'已禁用' }]"
                :key="opt.v" class="rec-chip"
                :class="{ 'rec-chip--active': userStatusFilter === opt.v, [`rec-chip--${opt.v === 'active' ? 'completed' : opt.v === 'disabled' ? 'failed' : 'all'}`]: true }"
                @click="userStatusFilter = opt.v">{{ opt.label }}</button>
            </div>
            <button class="mgmt-icon-btn" @click="loadUsers" title="刷新">
              <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><path d="M4 4a8 8 0 1 1-1 5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/><path d="M1 2v5h5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>
            </button>
            <button class="rec-export-btn" @click="exportUsersCSV">
              <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><path d="M10 3v10M6 9l4 4 4-4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/><path d="M3 15h14" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
              导出 CSV
            </button>
          </div>

          <!-- 批量操作浮条 -->
          <transition name="batch-slide">
            <div v-if="userSelection.length > 0" class="mgmt-batch-bar">
              <span class="mgmt-batch-info">
                <svg viewBox="0 0 20 20" fill="none" width="14" height="14"><path d="M9 12l2 2 4-4" stroke="#10b981" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><circle cx="10" cy="10" r="8" stroke="#10b981" stroke-width="1.6"/></svg>
                已选中 <b>{{ userSelection.length }}</b> 位用户
              </span>
              <div class="mgmt-batch-actions">
                <button class="mgmt-batch-btn mgmt-batch-btn--ok" @click="batchUserStatus(false)">
                  <svg viewBox="0 0 16 16" fill="none" width="13" height="13"><path d="M3 8l3 3 7-7" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>
                  批量启用
                </button>
                <button class="mgmt-batch-btn mgmt-batch-btn--err" @click="batchUserStatus(true)">
                  <svg viewBox="0 0 16 16" fill="none" width="13" height="13"><circle cx="8" cy="8" r="6" stroke="currentColor" stroke-width="1.6"/><path d="M8 5v3M8 11h.01" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>
                  批量禁用
                </button>
                <button class="mgmt-batch-btn mgmt-batch-btn--ghost" @click="userSelection = []">取消</button>
              </div>
            </div>
          </transition>

          <!-- 表头 -->
          <div class="mgmt-table-head">
            <div class="mgmt-chk-col">
              <input type="checkbox"
                class="mgmt-checkbox"
                :checked="pagedUsers.length > 0 && pagedUsers.filter(r=>r.id!==currentUser?.id).every(r=>userSelection.some(s=>s.id===r.id))"
                @change="e => { if(e.target.checked){ userSelection=[...userSelection,...pagedUsers.filter(r=>r.id!==currentUser?.id&&!userSelection.some(s=>s.id===r.id))] } else { userSelection=userSelection.filter(s=>!pagedUsers.some(r=>r.id===s.id)) } }"
              />
            </div>
            <div class="mgmt-col mgmt-col--id">ID</div>
            <div class="mgmt-col mgmt-col--user">用户</div>
            <div class="mgmt-col mgmt-col--email">邮箱</div>
            <div class="mgmt-col mgmt-col--role">角色</div>
            <div class="mgmt-col mgmt-col--status">状态</div>
            <div class="mgmt-col mgmt-col--date">注册时间</div>
            <div class="mgmt-col mgmt-col--date">最近登录</div>
            <div class="mgmt-col mgmt-col--action">操作</div>
          </div>

          <!-- 用户行列表 -->
          <div class="mgmt-list" v-loading="usersLoading" element-loading-text="用户加载中…">
            <div v-if="!usersLoading && pagedUsers.length === 0" class="rec-empty">
              <svg viewBox="0 0 64 64" fill="none" width="52" height="52"><circle cx="32" cy="24" r="10" stroke="#94a3b8" stroke-width="2"/><path d="M10 52c0-12 10-20 22-20s22 8 22 20" stroke="#94a3b8" stroke-width="2" stroke-linecap="round"/></svg>
              <p>{{ userSearchQuery || userStatusFilter !== 'all' ? '没有匹配用户' : '暂无用户数据' }}</p>
            </div>
            <div
              v-for="row in pagedUsers"
              :key="row.id"
              class="mgmt-row"
              :class="{ 'mgmt-row--disabled': row.isDisabled, 'mgmt-row--self': row.id === currentUser?.id }"
            >
              <div class="mgmt-chk-col">
                <input type="checkbox"
                  class="mgmt-checkbox"
                  :disabled="row.id === currentUser?.id"
                  :checked="userSelection.some(s => s.id === row.id)"
                  @change="e => { if(e.target.checked){ if(!userSelection.some(s=>s.id===row.id)) userSelection=[...userSelection,row] } else { userSelection=userSelection.filter(s=>s.id!==row.id) } }"
                />
              </div>
              <div class="mgmt-col mgmt-col--id">
                <span class="rec-id-badge">#{{ row.id }}</span>
              </div>
              <div class="mgmt-col mgmt-col--user">
                <div class="mgmt-avatar" :class="{ 'mgmt-avatar--admin': row.isAdmin }">
                  {{ (row.username || '?')[0].toUpperCase() }}
                </div>
                <span class="mgmt-username">{{ row.username || '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col--email">
                <span class="mgmt-email">{{ row.email || '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col--role">
                <span class="mgmt-role-badge" :class="row.isAdmin ? 'mgmt-role--admin' : 'mgmt-role--user'">
                  {{ row.isAdmin ? '管理员' : '普通用户' }}
                </span>
              </div>
              <div class="mgmt-col mgmt-col--status">
                <span class="mgmt-status-badge" :class="row.isDisabled ? 'mgmt-status--off' : 'mgmt-status--on'">
                  <span class="rec-status-dot"></span>
                  {{ row.isDisabled ? '已禁用' : '正常' }}
                </span>
              </div>
              <div class="mgmt-col mgmt-col--date">
                <span class="rec-time">{{ row.createdAt ? formatDate(row.createdAt) : '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col--date">
                <span class="rec-time">{{ row.lastLogin ? formatDate(row.lastLogin) : '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col--action">
                <button
                  class="mgmt-action-btn"
                  :class="row.isDisabled ? 'mgmt-action-btn--ok' : 'mgmt-action-btn--danger'"
                  :disabled="row.id === currentUser?.id"
                  @click="toggleUserStatus(row)"
                >{{ row.isDisabled ? '启用' : '禁用' }}</button>
              </div>
            </div>
          </div>

          <!-- 分页 -->
          <div class="rec-footer">
            <span class="rec-footer__count">共 <b>{{ filteredUsers.length }}</b> 位用户</span>
            <div class="rec-pagination">
              <button class="rec-page-btn" :disabled="userPage <= 1" @click="userPage--">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M10 4L6 8l4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
              <template v-for="p in userPaginationPages" :key="p">
                <span v-if="p === '…'" class="rec-page-ellipsis">…</span>
                <button v-else class="rec-page-btn rec-page-btn--num" :class="{ 'rec-page-btn--active': p === userPage }" @click="userPage = p">{{ p }}</button>
              </template>
              <button class="rec-page-btn" :disabled="userPage >= userTotalPages" @click="userPage++">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M6 4l4 4-4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
            </div>
          </div>
        </section>

        <!-- ── 偏好洞察（含数据看板） ────────────────────────────────────────── -->
        <section v-show="activeNav === 'insights'" class="ins-shell">

          <!-- 顶部 Hero Header -->
          <div class="ins-hero">
            <div class="ins-hero__text">
              <div class="ins-hero__eyebrow">ANALYTICS INSIGHT</div>
              <h2 class="ins-hero__title">偏好洞察</h2>
              <p class="ins-hero__sub">行为分析 · 数据统计 · 实时可视化</p>
            </div>
            <div class="ins-hero__actions">
              <!-- 时间范围（驱动数据看板部分） -->
              <el-select
                v-model="timeRange"
                size="small"
                class="ins-range-select"
                @change="loadMetrics"
              >
                <el-option label="最近 24 小时" value="day" />
                <el-option label="最近 7 天" value="week" />
                <el-option label="最近 30 天" value="month" />
              </el-select>
              <button class="ins-refresh-btn" @click="refreshInsights" :class="{ spinning: insightsBusy }">
                <el-icon><Refresh /></el-icon>
                <span>{{ insightsBusy ? '加载中…' : '刷新数据' }}</span>
              </button>
            </div>
          </div>

          <!-- 统一骨架屏：两套数据任意一套未就绪时展示 -->
          <div v-if="insightsBusy" class="ins-skeleton-wrap">
            <div class="ins-skeleton-row">
              <div class="ins-skeleton-kpi" v-for="i in 4" :key="i"></div>
            </div>
            <div class="ins-skeleton-row ins-skeleton-row--charts">
              <div class="ins-skeleton-chart ins-skeleton-chart--half"></div>
              <div class="ins-skeleton-chart ins-skeleton-chart--half"></div>
            </div>
            <div class="ins-skeleton-row ins-skeleton-row--charts">
              <div class="ins-skeleton-chart ins-skeleton-chart--half"></div>
              <div class="ins-skeleton-chart ins-skeleton-chart--half"></div>
            </div>
            <div class="ins-skeleton-row">
              <div class="ins-skeleton-chart ins-skeleton-chart--full"></div>
            </div>
          </div>

          <!-- 有洞察数据时渲染 -->
          <template v-else-if="insightsData">

            <!-- KPI 摘要卡 -->
            <div class="ins-kpi-strip">
              <div class="ins-kpi-card ins-kpi-card--blue">
                <div class="ins-kpi-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>
                </div>
                <div class="ins-kpi-body">
                  <span class="ins-kpi-value">{{ (insightsData.userFunnel?.registered || 0).toLocaleString() }}</span>
                  <span class="ins-kpi-label">注册用户</span>
                </div>
                <div class="ins-kpi-trend">
                  <span class="ins-kpi-sub">已生成 PPT</span>
                  <span class="ins-kpi-pct">{{ insightsData.userFunnel?.generatedOnce || 0 }} 人</span>
                </div>
              </div>
              <div class="ins-kpi-card ins-kpi-card--violet">
                <div class="ins-kpi-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M9 21V9"/></svg>
                </div>
                <div class="ins-kpi-body">
                  <span class="ins-kpi-value">{{ totalGenerations.toLocaleString() }}</span>
                  <span class="ins-kpi-label">总生成次数</span>
                </div>
                <div class="ins-kpi-trend">
                  <span class="ins-kpi-sub">高频用户</span>
                  <span class="ins-kpi-pct">{{ insightsData.userFunnel?.generatedMulti || 0 }} 人</span>
                </div>
              </div>
              <div class="ins-kpi-card ins-kpi-card--teal">
                <div class="ins-kpi-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>
                </div>
                <div class="ins-kpi-body">
                  <span class="ins-kpi-value">{{ topModelName }}</span>
                  <span class="ins-kpi-label">最常用模型</span>
                </div>
                <div class="ins-kpi-trend">
                  <span class="ins-kpi-sub">占比</span>
                  <span class="ins-kpi-pct">{{ topModelPct }}%</span>
                </div>
              </div>
              <div class="ins-kpi-card ins-kpi-card--amber">
                <div class="ins-kpi-icon">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
                </div>
                <div class="ins-kpi-body">
                  <span class="ins-kpi-value">{{ peakHour }}</span>
                  <span class="ins-kpi-label">生成高峰时段</span>
                </div>
                <div class="ins-kpi-trend">
                  <span class="ins-kpi-sub">峰值</span>
                  <span class="ins-kpi-pct">{{ peakCount }} 次</span>
                </div>
              </div>
            </div>

            <!-- 图表网格：全量图表统一排布 -->
            <div class="ins-charts-grid">

              <!-- 用户转化漏斗 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--blue"></div>
                  <span class="ins-chart-title">用户转化漏斗</span>
                </div>
                <ElChart :option="funnelChartOption" :height="260" />
              </div>

              <!-- 模型使用分布 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--violet"></div>
                  <span class="ins-chart-title">AI 模型使用分布</span>
                </div>
                <ElChart :option="modelPieChartOption" :height="260" />
              </div>

              <!-- 热门关键词词云 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--teal"></div>
                  <span class="ins-chart-title">热门关键词词云</span>
                </div>
                <div class="word-cloud-wrap">
                  <canvas ref="wordCloudCanvas" class="word-cloud-canvas" />
                  <div v-if="!insightsData?.topTopics?.length" class="word-cloud-empty">
                    <span>暂无数据</span>
                  </div>
                </div>
              </div>

              <!-- 页数分布 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--amber"></div>
                  <span class="ins-chart-title">生成页数分布</span>
                </div>
                <ElChart :option="pagesBarChartOption" :height="300" />
              </div>

              <!-- 高峰热力图（全宽） -->
              <div class="ins-chart-card ins-chart-card--full">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--rose"></div>
                  <span class="ins-chart-title">生成高峰时段热力图 &nbsp;<span class="ins-chart-badge">24h × 7 天</span></span>
                </div>
                <ElChart :option="heatmapChartOption" :height="200" />
              </div>

              <!-- 用户活跃度（可切换图表类型） -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--blue"></div>
                  <span class="ins-chart-title">用户活跃度</span>
                  <div class="ins-chart-type-select">
                    <el-select v-model="chartCards[0].chartType" size="small">
                      <el-option v-for="t in chartCards[0].types" :key="t" :label="chartTypeLabels[t]" :value="t" />
                    </el-select>
                  </div>
                </div>
                <ElChart
                  :ref="(el) => setChartRef('activity', el)"
                  :option="chartOptions['activity']"
                  :height="280"
                />
              </div>

              <!-- 生成频率 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--violet"></div>
                  <span class="ins-chart-title">生成频率</span>
                  <div class="ins-chart-type-select">
                    <el-select v-model="chartCards[1].chartType" size="small">
                      <el-option v-for="t in chartCards[1].types" :key="t" :label="chartTypeLabels[t]" :value="t" />
                    </el-select>
                  </div>
                </div>
                <ElChart
                  :ref="(el) => setChartRef('generation', el)"
                  :option="chartOptions['generation']"
                  :height="280"
                />
              </div>

              <!-- 模板偏好分布 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--teal"></div>
                  <span class="ins-chart-title">模板偏好分布</span>
                  <div class="ins-chart-type-select">
                    <el-select v-model="chartCards[2].chartType" size="small">
                      <el-option v-for="t in chartCards[2].types" :key="t" :label="chartTypeLabels[t]" :value="t" />
                    </el-select>
                  </div>
                </div>
                <ElChart
                  :ref="(el) => setChartRef('templateShare', el)"
                  :option="chartOptions['templateShare']"
                  :height="280"
                />
              </div>

              <!-- 生成成功率 -->
              <div class="ins-chart-card ins-chart-card--half">
                <div class="ins-chart-header">
                  <div class="ins-chart-dot ins-chart-dot--amber"></div>
                  <span class="ins-chart-title">生成成功率</span>
                  <div class="ins-chart-type-select">
                    <el-select v-model="chartCards[3].chartType" size="small">
                      <el-option v-for="t in chartCards[3].types" :key="t" :label="chartTypeLabels[t]" :value="t" />
                    </el-select>
                  </div>
                </div>
                <ElChart
                  :ref="(el) => setChartRef('successRate', el)"
                  :option="chartOptions['successRate']"
                  :height="280"
                />
              </div>

            </div>
          </template>

          <!-- 空状态 -->
          <div v-else class="ins-empty">
            <svg class="ins-empty-icon" viewBox="0 0 80 80" fill="none"><circle cx="40" cy="40" r="38" stroke="#e0f2fe" stroke-width="4"/><path d="M25 55 Q40 25 55 55" stroke="#7dd3fc" stroke-width="3" stroke-linecap="round" fill="none"/><circle cx="40" cy="32" r="5" fill="#7dd3fc"/></svg>
            <p class="ins-empty-title">暂无洞察数据</p>
            <p class="ins-empty-sub">请先生成一些 PPT，数据积累后将自动展示行为分析。</p>
            <button class="ins-refresh-btn" @click="refreshInsights">重新加载</button>
          </div>

        </section>

        <!-- ══ 公告与通知管理 ══════════════════════════════════════════════════ -->
        <section v-show="activeNav === 'announcements'" class="mgmt-shell">

          <!-- Hero -->
          <div class="mgmt-hero mgmt-hero--violet">
            <div class="mgmt-hero__left">
              <div class="mgmt-hero__eyebrow">ANNOUNCEMENTS</div>
              <h2 class="mgmt-hero__title">公告与通知管理</h2>
              <p class="mgmt-hero__sub">站内公告发布 · 有效期管理 · 置顶优先展示</p>
            </div>
            <button class="ann-publish-btn" @click="openAnnouncementDialog()">
              <svg viewBox="0 0 20 20" fill="none" width="16" height="16"><path d="M10 4v12M4 10h12" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>
              发布新公告
            </button>
          </div>

          <!-- 公告卡片列表 -->
          <div class="ann-list" v-loading="announcementsLoading" element-loading-text="加载中…">
            <div v-if="!announcementsLoading && announcements.length === 0" class="rec-empty">
              <svg viewBox="0 0 64 64" fill="none" width="52" height="52"><path d="M8 16h48v32a4 4 0 0 1-4 4H12a4 4 0 0 1-4-4V16z" stroke="#94a3b8" stroke-width="2"/><path d="M8 16l24 20 24-20" stroke="#94a3b8" stroke-width="2" stroke-linecap="round"/></svg>
              <p>暂无公告</p>
              <button class="rec-reset-link" @click="openAnnouncementDialog()">发布第一条公告</button>
            </div>
            <div
              v-for="ann in announcements"
              :key="ann.id"
              class="ann-card"
              :class="{
                'ann-card--pinned': ann.is_pinned,
                'ann-card--expired': announcementStatusText(ann) === '已过期',
                'ann-card--pending': announcementStatusText(ann) === '未开始'
              }"
            >
              <!-- 置顶标记 -->
              <div v-if="ann.is_pinned" class="ann-pin-badge">
                <svg viewBox="0 0 16 16" fill="currentColor" width="11" height="11"><path d="M9.5 1L14 5.5 9.5 7l-2 2-1-1L8 6.5 3.5 6 8 1.5 9.5 1z"/><path d="M6 10l-4 4" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
                置顶
              </div>

              <!-- 主内容 -->
              <div class="ann-card__body">
                <div class="ann-card__top">
                  <span class="ann-status-badge" :class="`ann-status--${announcementStatusText(ann) === '生效中' ? 'active' : announcementStatusText(ann) === '已过期' ? 'expired' : 'pending'}`">
                    <span class="rec-status-dot"></span>
                    {{ announcementStatusText(ann) }}
                  </span>
                  <h3 class="ann-title">{{ ann.title }}</h3>
                </div>
                <p class="ann-content">{{ ann.content }}</p>
                <div class="ann-meta">
                  <span class="ann-meta-item">
                    <svg viewBox="0 0 16 16" fill="none" width="12" height="12"><circle cx="8" cy="8" r="6" stroke="currentColor" stroke-width="1.4"/><path d="M8 5v3l2 2" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
                    生效：{{ formatDate(ann.starts_at) }}
                  </span>
                  <span class="ann-meta-item">
                    <svg viewBox="0 0 16 16" fill="none" width="12" height="12"><circle cx="8" cy="8" r="6" stroke="currentColor" stroke-width="1.4"/><path d="M6 6l4 4M10 6l-4 4" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>
                    过期：{{ ann.expires_at ? formatDate(ann.expires_at) : '永不过期' }}
                  </span>
                </div>
              </div>

              <!-- 操作 -->
              <div class="ann-card__actions">
                <button class="ann-act-btn ann-act-btn--edit" @click="openAnnouncementDialog(ann)">
                  <svg viewBox="0 0 16 16" fill="none" width="13" height="13"><path d="M11 2l3 3-8 8H3v-3l8-8z" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>
                  编辑
                </button>
                <button class="ann-act-btn ann-act-btn--del" @click="deleteAnnouncement(ann)">
                  <svg viewBox="0 0 16 16" fill="none" width="13" height="13"><path d="M2 4h12M6 4V3h4v1M5 4v8a1 1 0 0 0 1 1h4a1 1 0 0 0 1-1V4" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>
                  删除
                </button>
              </div>
            </div>
          </div>

          <div class="rec-footer" v-if="announcementTotal > announcementPageSize">
            <span class="rec-footer__count">共 <b>{{ announcementTotal }}</b> 条公告</span>
            <div class="rec-pagination">
              <button class="rec-page-btn" :disabled="announcementPage <= 1" @click="announcementPage--; loadAnnouncements()">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M10 4L6 8l4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
              <span class="rec-page-btn rec-page-btn--num rec-page-btn--active">{{ announcementPage }}</span>
              <button class="rec-page-btn" :disabled="announcementPage * announcementPageSize >= announcementTotal" @click="announcementPage++; loadAnnouncements()">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M6 4l4 4-4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
            </div>
          </div>
        </section>

        <!-- 创建/编辑公告弹窗 -->
        <el-dialog
          v-model="annDialog.visible"
          :title="annDialog.isEdit ? '编辑公告' : '发布公告'"
          width="560px"
          draggable
        >
          <el-form :model="annDialog.form" label-width="90px" style="padding: 0 12px;">
            <el-form-item label="标题" required>
              <el-input v-model="annDialog.form.title" maxlength="200" show-word-limit placeholder="请输入公告标题" />
            </el-form-item>
            <el-form-item label="内容" required>
              <el-input
                v-model="annDialog.form.content"
                type="textarea"
                :rows="5"
                maxlength="2000"
                show-word-limit
                placeholder="请输入公告内容"
              />
            </el-form-item>
            <el-form-item label="置顶">
              <el-switch v-model="annDialog.form.is_pinned" />
            </el-form-item>
            <el-form-item label="生效时间">
              <el-date-picker
                v-model="annDialog.form.starts_at"
                type="datetime"
                placeholder="默认立即生效"
                format="YYYY/MM/DD HH:mm"
                value-format="YYYY-MM-DD HH:mm:ss"
                style="width: 100%;"
              />
            </el-form-item>
            <el-form-item label="过期时间">
              <el-date-picker
                v-model="annDialog.form.expires_at"
                type="datetime"
                placeholder="留空则永不过期"
                format="YYYY/MM/DD HH:mm"
                value-format="YYYY-MM-DD HH:mm:ss"
                style="width: 100%;"
              />
            </el-form-item>
          </el-form>
          <template #footer>
            <el-button @click="annDialog.visible = false">取消</el-button>
            <el-button type="primary" :loading="annDialog.saving" @click="submitAnnouncement">
              {{ annDialog.isEdit ? '保存修改' : '发布' }}
            </el-button>
          </template>
        </el-dialog>

        <!-- ══ 素材全局管理 ═══════════════════════════════════════════════════ -->
        <section v-show="activeNav === 'materials'" class="mgmt-shell">

          <!-- Hero -->
          <div class="mgmt-hero mgmt-hero--amber">
            <div class="mgmt-hero__left">
              <div class="mgmt-hero__eyebrow">MATERIAL MANAGEMENT</div>
              <h2 class="mgmt-hero__title">素材全局管理</h2>
              <p class="mgmt-hero__sub">文件检索 · AI 审核 · 全局清理</p>
            </div>
            <div class="mgmt-hero__stats" v-if="materialStats.total >= 0">
              <div class="mgmt-stat-pill">
                <span class="mgmt-stat-num">{{ materialStats.total }}</span>
                <span class="mgmt-stat-label">总素材</span>
              </div>
              <div class="mgmt-stat-pill mgmt-stat-pill--ok">
                <span class="mgmt-stat-num">{{ materialStats.completed }}</span>
                <span class="mgmt-stat-label">已完成</span>
              </div>
              <div class="mgmt-stat-pill mgmt-stat-pill--warn">
                <span class="mgmt-stat-num">{{ materialStats.pending }}</span>
                <span class="mgmt-stat-label">处理中</span>
              </div>
              <div class="mgmt-stat-pill mgmt-stat-pill--err">
                <span class="mgmt-stat-num">{{ materialStats.failed }}</span>
                <span class="mgmt-stat-label">失败</span>
              </div>
            </div>
          </div>

          <!-- 空间占用条 -->
          <div class="mat-storage-bar" v-if="materialStats.total >= 0">
            <svg viewBox="0 0 20 20" fill="none" width="15" height="15" style="flex-shrink:0;color:#f59e0b">
              <path d="M2 8h16M6 4h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>
            </svg>
            <span class="mat-storage-label">存储占用</span>
            <span class="mat-storage-value">{{ formatBytes(materialStats.totalSize) }}</span>
          </div>

          <!-- 工具栏 -->
          <div class="mgmt-toolbar">
            <div class="rec-search-wrap" style="min-width:100px;max-width:160px;flex:none">
              <svg class="rec-search-icon" viewBox="0 0 20 20" fill="none">
                <circle cx="8.5" cy="8.5" r="5.5" stroke="currentColor" stroke-width="1.6"/>
              </svg>
              <input v-model="materialSearch.userId" class="rec-search-input" placeholder="用户 ID" style="padding-left:32px" />
            </div>
            <div class="rec-filter-chips">
              <button
                v-for="opt in [{ v:'',label:'全部' },{ v:'completed',label:'已完成' },{ v:'pending',label:'处理中' },{ v:'failed',label:'失败' }]"
                :key="opt.v" class="rec-chip"
                :class="{ 'rec-chip--active': materialSearch.status === opt.v,
                  'rec-chip--all': opt.v==='',
                  'rec-chip--completed': opt.v==='completed',
                  'rec-chip--pending': opt.v==='pending',
                  'rec-chip--failed': opt.v==='failed' }"
                @click="materialSearch.status = opt.v; loadMaterials(1)"
              >{{ opt.label }}</button>
            </div>
            <div class="rec-filter-chips">
              <button
                v-for="ft in [{ v:'',label:'全类型' },{ v:'pdf',label:'PDF' },{ v:'docx',label:'DOCX' },{ v:'txt',label:'TXT' }]"
                :key="ft.v" class="rec-chip"
                :class="{ 'rec-chip--active rec-chip--all': materialSearch.fileType === ft.v }"
                @click="materialSearch.fileType = ft.v; loadMaterials(1)"
              >{{ ft.label }}</button>
            </div>
            <button class="mgmt-icon-btn" @click="loadMaterials(1)" title="搜索">
              <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><circle cx="8.5" cy="8.5" r="5.5" stroke="currentColor" stroke-width="1.8"/><path d="M13 13l3.5 3.5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
            </button>
            <button
              class="mgmt-danger-btn"
              :disabled="materialSelection.length === 0"
              @click="batchDeleteMaterials"
            >
              <svg viewBox="0 0 16 16" fill="none" width="13" height="13"><path d="M3 4h10M6 4V3h4v1M5 4v8a1 1 0 0 0 1 1h4a1 1 0 0 0 1-1V4" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>
              批量删除{{ materialSelection.length > 0 ? ` (${materialSelection.length})` : '' }}
            </button>
          </div>

          <!-- 表头 -->
          <div class="mgmt-table-head mgmt-table-head--mat">
            <div class="mgmt-chk-col">
              <input type="checkbox" class="mgmt-checkbox"
                :checked="materials.length > 0 && materials.every(r => materialSelection.some(s => s.id === r.id))"
                @change="e => { materialSelection = e.target.checked ? [...materials] : [] }"
              />
            </div>
            <div class="mgmt-col">文件名</div>
            <div class="mgmt-col">用户</div>
            <div class="mgmt-col">类型</div>
            <div class="mgmt-col">大小</div>
            <div class="mgmt-col">状态</div>
            <div class="mgmt-col">上传时间</div>
            <div class="mgmt-col">操作</div>
          </div>

          <!-- 素材行列表 -->
          <div class="mgmt-list" v-loading="materialsLoading" element-loading-text="加载中…">
            <div v-if="!materialsLoading && materials.length === 0" class="rec-empty">
              <svg viewBox="0 0 64 64" fill="none" width="52" height="52"><rect x="12" y="8" width="40" height="48" rx="4" stroke="#94a3b8" stroke-width="2"/><path d="M20 20h24M20 28h16M20 36h10" stroke="#94a3b8" stroke-width="2" stroke-linecap="round"/></svg>
              <p>暂无素材数据</p>
            </div>
            <div v-for="row in materials" :key="row.id" class="mgmt-row mgmt-row--mat">
              <div class="mgmt-chk-col">
                <input type="checkbox" class="mgmt-checkbox"
                  :checked="materialSelection.some(s => s.id === row.id)"
                  @change="e => { if(e.target.checked){ if(!materialSelection.some(s=>s.id===row.id)) materialSelection=[...materialSelection,row] } else { materialSelection=materialSelection.filter(s=>s.id!==row.id) } }"
                />
              </div>
              <div class="mgmt-col">
                <div class="mat-file-icon" :class="`mat-file-icon--${row.fileType || 'txt'}`">
                  {{ (row.fileType || 'file').toUpperCase().slice(0,3) }}
                </div>
                <span class="mat-filename" :title="row.filename">{{ row.filename || '—' }}</span>
              </div>
              <div class="mgmt-col">
                <div class="mgmt-avatar" style="width:22px;height:22px;font-size:10px;margin-right:6px">
                  {{ (row.username || '?')[0].toUpperCase() }}
                </div>
                <span class="mgmt-username">{{ row.username || '—' }}</span>
              </div>
              <div class="mgmt-col">
                <span class="mat-type-badge mat-type-badge--{{ (row.fileType||'').toLowerCase() }}">
                  {{ row.fileType?.toUpperCase() || '—' }}
                </span>
              </div>
              <div class="mgmt-col">
                <span class="rec-time">{{ formatBytes(row.fileSize) }}</span>
              </div>
              <div class="mgmt-col">
                <span class="rec-status-badge" :class="`rec-status--${row.status}`">
                  <span class="rec-status-dot"></span>
                  {{ materialStatusText(row.status) }}
                </span>
              </div>
              <div class="mgmt-col">
                <span class="rec-time">{{ row.createdAt ? dayjs.unix(row.createdAt).format('YYYY/MM/DD HH:mm') : '—' }}</span>
              </div>
              <div class="mgmt-col">
                <div class="mat-action-group">
                  <button class="mat-act-btn mat-act-btn--view" @click="openMaterialDrawer(row, 'preview')">预览</button>
                  <button class="mat-act-btn mat-act-btn--review" @click="openMaterialDrawer(row, 'review')">审核</button>
                  <button class="mat-act-btn mat-act-btn--del" @click="deleteMaterial(row)">删除</button>
                </div>
              </div>
            </div>
          </div>

          <!-- 分页 -->
          <div class="rec-footer">
            <span class="rec-footer__count">共 <b>{{ materialTotal }}</b> 条记录</span>
            <div class="rec-pagination">
              <button class="rec-page-btn" :disabled="materialPage <= 1" @click="materialPage--; loadMaterials(materialPage)">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M10 4L6 8l4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
              <span class="rec-page-btn rec-page-btn--num rec-page-btn--active">{{ materialPage }}</span>
              <button class="rec-page-btn" :disabled="materialPage * materialPageSize >= materialTotal" @click="materialPage++; loadMaterials(materialPage)">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M6 4l4 4-4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
            </div>
          </div>
        </section>

        <!-- ══ 操作审计日志 ════════════════════════════════════════════════════ -->
        <section v-show="activeNav === 'audit'" class="mgmt-shell">

          <!-- Hero -->
          <div class="mgmt-hero mgmt-hero--slate">
            <div class="mgmt-hero__left">
              <div class="mgmt-hero__eyebrow">AUDIT LOG</div>
              <h2 class="mgmt-hero__title">操作审计日志</h2>
              <p class="mgmt-hero__sub">全量操作溯源 · 安全事件追踪 · 合规留存</p>
            </div>
            <div style="display:flex;gap:10px;align-items:center">
              <button class="mgmt-icon-btn" :class="{ spinning: auditLoading }" @click="loadAuditLogs(auditPage)" title="刷新">
                <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><path d="M4 4a8 8 0 1 1-1 5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/><path d="M1 2v5h5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
              <button class="rec-export-btn" @click="exportAuditCSV">
                <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><path d="M10 3v10M6 9l4 4 4-4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/><path d="M3 15h14" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
                导出 CSV
              </button>
            </div>
          </div>

          <!-- 筛选工具栏 -->
          <div class="mgmt-toolbar">
            <div class="rec-search-wrap">
              <svg class="rec-search-icon" viewBox="0 0 20 20" fill="none">
                <circle cx="8.5" cy="8.5" r="5.5" stroke="currentColor" stroke-width="1.6"/>
                <path d="M13 13l3.5 3.5" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>
              </svg>
              <input v-model="auditFilter.keyword" class="rec-search-input" placeholder="搜索操作人 / 对象 ID…"
                @keyup.enter="loadAuditLogs(1)" />
              <button v-if="auditFilter.keyword" class="rec-search-clear" @click="auditFilter.keyword=''; loadAuditLogs(1)">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>
              </button>
            </div>
            <el-select v-model="auditFilter.action" placeholder="操作类型" clearable size="default"
              style="width:170px" @change="loadAuditLogs(1)">
              <el-option v-for="opt in auditActionOptions" :key="opt.value" :label="opt.label" :value="opt.value" />
            </el-select>
            <el-date-picker v-model="auditFilter.dateRange" type="daterange" size="default"
              range-separator="→" start-placeholder="开始" end-placeholder="结束"
              value-format="YYYY-MM-DD" class="rec-datepicker" style="width:220px"
              @change="loadAuditLogs(1)" />
            <button class="mgmt-icon-btn" @click="loadAuditLogs(1)" title="查询">
              <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><circle cx="8.5" cy="8.5" r="5.5" stroke="currentColor" stroke-width="1.8"/><path d="M13 13l3.5 3.5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
            </button>
          </div>

          <!-- 表头 -->
          <div class="mgmt-table-head mgmt-table-head--audit">
            <div class="mgmt-col mgmt-col-audit--idx">#</div>
            <div class="mgmt-col mgmt-col-audit--operator">操作人</div>
            <div class="mgmt-col mgmt-col-audit--action">操作类型</div>
            <div class="mgmt-col mgmt-col-audit--target">对象类型</div>
            <div class="mgmt-col mgmt-col-audit--targetid">对象 ID</div>
            <div class="mgmt-col mgmt-col-audit--detail">详情</div>
            <div class="mgmt-col mgmt-col-audit--ip">来源 IP</div>
            <div class="mgmt-col mgmt-col-audit--time">时间</div>
          </div>

          <!-- 日志行列表 -->
          <div class="mgmt-list" v-loading="auditLoading" element-loading-text="加载中…">
            <div v-if="!auditLoading && auditLogs.length === 0" class="rec-empty">
              <svg viewBox="0 0 64 64" fill="none" width="52" height="52"><rect x="8" y="8" width="48" height="48" rx="6" stroke="#94a3b8" stroke-width="2"/><path d="M20 24h24M20 32h16M20 40h10" stroke="#94a3b8" stroke-width="2" stroke-linecap="round"/></svg>
              <p>暂无审计日志</p>
            </div>
            <div
              v-for="(log, idx) in auditLogs"
              :key="log.id || idx"
              class="mgmt-row mgmt-row--audit"
            >
              <div class="mgmt-col mgmt-col-audit--idx audit-idx">
                {{ (auditPage - 1) * auditPageSize + idx + 1 }}
              </div>
              <div class="mgmt-col mgmt-col-audit--operator">
                <div class="mgmt-avatar" style="width:24px;height:24px;font-size:10px;margin-right:7px">
                  {{ (log.operator || '?')[0].toUpperCase() }}
                </div>
                <span class="mgmt-username">{{ log.operator || '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col-audit--action">
                <span class="audit-action-badge" :class="`audit-action--${auditActionTagType(log.action)}`">
                  {{ auditActionLabel(log.action) }}
                </span>
              </div>
              <div class="mgmt-col mgmt-col-audit--target">
                <span class="audit-target-chip">{{ log.targetType || '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col-audit--targetid">
                <span class="audit-id-text" :title="log.targetId">{{ log.targetId || '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col-audit--detail">
                <div
                  class="audit-detail-text"
                  :class="{ 'audit-detail-text--empty': !auditDetailHasBody(log.detail) }"
                >{{ auditDetailDisplay(log.detail) }}</div>
              </div>
              <div class="mgmt-col mgmt-col-audit--ip">
                <span class="audit-ip" :title="log.ip || ''">{{ log.ip || '—' }}</span>
              </div>
              <div class="mgmt-col mgmt-col-audit--time">
                <span class="rec-time">{{ log.createdAt || '—' }}</span>
              </div>
            </div>
          </div>

          <!-- 分页 -->
          <div class="rec-footer" v-if="auditTotal > auditPageSize">
            <span class="rec-footer__count">共 <b>{{ auditTotal }}</b> 条日志</span>
            <div class="rec-pagination">
              <button class="rec-page-btn" :disabled="auditPage <= 1" @click="loadAuditLogs(auditPage - 1)">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M10 4L6 8l4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
              <span class="rec-page-btn rec-page-btn--num rec-page-btn--active">{{ auditPage }}</span>
              <button class="rec-page-btn" :disabled="auditPage * auditPageSize >= auditTotal" @click="loadAuditLogs(auditPage + 1)">
                <svg viewBox="0 0 16 16" fill="none" width="14" height="14"><path d="M6 4l4 4-4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
            </div>
          </div>
        </section>

        <!-- ── 系统配置中心 ────────────────────────────────────────────────── -->
        <section v-show="activeNav === 'settings'" class="settings-section">
          <!-- 头部 -->
          <div class="settings-header">
            <div class="settings-header-left">
              <div class="settings-title-row">
                <div class="settings-icon-wrap">
                  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07M4.93 4.93a10 10 0 0 0 0 14.14M8.46 8.46a5 5 0 0 0 0 7.07"/></svg>
                </div>
                <h2 class="settings-title">系统配置中心</h2>
              </div>
              <p class="settings-subtitle">热更新配置无需重启服务，修改即刻生效</p>
            </div>
            <div class="settings-header-actions">
              <button class="settings-refresh-btn" :class="{ spinning: settingsLoading }" @click="loadSettings" title="刷新">
                <svg viewBox="0 0 20 20" fill="none" width="15" height="15"><path d="M4 4a8 8 0 1 1-1 5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/><path d="M1 2v5h5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>
              </button>
            </div>
          </div>

          <!-- 分组 Tab -->
          <div class="settings-tabs">
            <button
              v-for="tab in settingsTabs"
              :key="tab.id"
              class="settings-tab-btn"
              :class="{ active: settingsActiveTab === tab.id }"
              @click="settingsActiveTab = tab.id"
            >
              <span v-html="tab.icon" class="stab-icon"></span>
              {{ tab.label }}
            </button>
          </div>

          <!-- 加载状态 -->
          <div v-if="settingsLoading" class="settings-loading">
            <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" class="spin-svg"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>
            <span>加载配置中…</span>
          </div>

          <!-- 配置表单 -->
          <div v-else-if="settingsGrouped[settingsActiveTab]" class="settings-form-card">
            <div
              v-for="item in settingsGrouped[settingsActiveTab]"
              :key="item.key"
              class="settings-row"
            >
              <div class="settings-row-info">
                <div class="settings-row-label">{{ item.label }}</div>
                <div class="settings-row-desc">{{ item.description }}</div>
              </div>
              <div class="settings-row-ctrl">
                <!-- bool 类型：开关 -->
                <el-switch
                  v-if="item.type === 'bool'"
                  v-model="settingsDraft[item.key]"
                  active-color="#6366f1"
                  inactive-color="#e2e8f0"
                />
                <!-- int 类型：数字输入 -->
                <el-input-number
                  v-else-if="item.type === 'int'"
                  v-model="settingsDraft[item.key]"
                  :min="0"
                  :max="9999"
                  controls-position="right"
                  size="default"
                  style="width:140px"
                />
                <!-- string 类型：文本输入 -->
                <el-input
                  v-else
                  v-model="settingsDraft[item.key]"
                  size="default"
                  style="width:280px"
                  clearable
                />
              </div>
            </div>

            <!-- 更新时间提示 -->
            <div class="settings-updated-row">
              <span v-if="settingsLastSaved">
                上次保存于 {{ settingsLastSaved }}
              </span>
            </div>

            <!-- 保存按钮 -->
            <div class="settings-footer">
              <button
                class="settings-save-btn"
                :disabled="settingsSaving"
                @click="saveSettings"
              >
                <svg v-if="!settingsSaving" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/><polyline points="7 3 7 8 15 8"/></svg>
                <svg v-else width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" class="spin-svg"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>
                {{ settingsSaving ? '保存中…' : '保存配置' }}
              </button>
              <button class="settings-reset-btn" @click="resetDraft">
                撤销更改
              </button>
            </div>
          </div>

          <div v-else class="settings-empty">
            <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" opacity="0.3"><circle cx="12" cy="12" r="3"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>
            <p>暂无配置项</p>
          </div>
        </section>

        <!-- ══ 模板管理 ════════════════════════════════════════════════════════ -->
        <section v-show="activeNav === 'templates'" class="tmpl-mgr-section">

          <!-- 顶栏：标题 + 统计 + Tab 切换 -->
          <div class="tmpl-top-bar">
            <div class="tmpl-top-left">
              <div class="tmpl-top-title-row">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="tmpl-title-icon"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
                <h2 class="tmpl-mgr-title">模板管理</h2>
              </div>
              <p class="tmpl-mgr-subtitle">配置模板上架 / 下架，或从 OfficePLUS 导入新模板</p>
            </div>

            <!-- 统计徽章 -->
            <div class="tmpl-kpi-row">
              <div class="tmpl-kpi">
                <span class="tmpl-kpi-num">{{ tmplMgrList.length }}</span>
                <span class="tmpl-kpi-label">总计</span>
              </div>
              <div class="tmpl-kpi-divider"></div>
              <div class="tmpl-kpi tmpl-kpi--live">
                <span class="tmpl-kpi-num">{{ tmplMgrList.filter(t => t.isListed).length }}</span>
                <span class="tmpl-kpi-label">上架中</span>
              </div>
              <div class="tmpl-kpi-divider"></div>
              <div class="tmpl-kpi tmpl-kpi--off">
                <span class="tmpl-kpi-num">{{ tmplMgrList.filter(t => !t.isListed).length }}</span>
                <span class="tmpl-kpi-label">未上架</span>
              </div>
            </div>
          </div>

          <!-- 子标签 -->
          <div class="tmpl-tab-rail">
            <button
              class="tmpl-tab-btn"
              :class="{ 'is-active': tmplActiveTab === 'local' }"
              @click="tmplActiveTab = 'local'"
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
              本地模板库
            </button>
            <button
              class="tmpl-tab-btn tmpl-tab-btn--op"
              :class="{ 'is-active': tmplActiveTab === 'officeplus' }"
              @click="tmplActiveTab = 'officeplus'"
            >
              <img src="https://www.officeplus.cn/favicon.ico" class="op-favicon" alt="" @error="e => e.target.style.display='none'" />
              OfficePLUS 导入
            </button>
          </div>

          <!-- ─── 本地管理标签内容 ────────────────────────────────────────── -->
          <div v-show="tmplActiveTab === 'local'" class="tmpl-local-panel">

            <!-- 工具栏 -->
            <div class="tmpl-toolbar">
              <div class="tmpl-search-wrap">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" class="tmpl-search-icon"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
                <input
                  v-model="tmplMgrSearch"
                  class="tmpl-search-input"
                  placeholder="搜索模板名称…"
                />
              </div>
              <button class="tmpl-refresh-btn" @click="loadTemplateList" :disabled="tmplMgrLoading">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" :class="{ 'spin': tmplMgrLoading }"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg>
                刷新
              </button>
            </div>

            <!-- 骨架屏 -->
            <div v-if="tmplMgrLoading" class="tmpl-skeleton-grid">
              <div v-for="i in 6" :key="i" class="tmpl-skeleton-card">
                <div class="sk-thumb"></div>
                <div class="sk-body">
                  <div class="sk-line sk-line--title"></div>
                  <div class="sk-line sk-line--sub"></div>
                  <div class="sk-line sk-line--tags"></div>
                </div>
              </div>
            </div>

            <!-- 空态 -->
            <div v-else-if="tmplMgrFiltered.length === 0" class="tmpl-empty-state">
              <div class="tmpl-empty-icon">
                <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" opacity="0.5"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
              </div>
              <p class="tmpl-empty-text">暂无模板数据</p>
              <p class="tmpl-empty-sub">可前往 OfficePLUS 导入新模板</p>
            </div>

            <!-- 卡片网格 -->
            <div v-else class="tmpl-mgr-grid">
              <div
                v-for="tmpl in tmplMgrFiltered"
                :key="tmpl.id"
                class="tmpl-mgr-card"
                :class="{ 'is-listed': tmpl.isListed, 'is-unlisted': !tmpl.isListed }"
              >
                <!-- 预览图区域 -->
                <div class="tmpl-card-thumb">
                  <img
                    v-if="!tmplPreviewFailed.has(tmpl.id)"
                    :src="resolveTmplPreviewUrl(tmpl)"
                    :alt="tmpl.name"
                    class="tmpl-preview-img"
                    @error="tmplPreviewFailed.add(tmpl.id)"
                  />
                  <div v-else class="tmpl-preview-placeholder">
                    <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" opacity="0.35"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
                  </div>
                  <!-- 状态指示条 -->
                  <div class="tmpl-status-strip" :class="tmpl.isListed ? 'strip--live' : 'strip--off'">
                    <span class="strip-dot"></span>
                    {{ tmpl.isListed ? '上架中' : '未上架' }}
                  </div>
                </div>

                <!-- 信息主体 -->
                <div class="tmpl-card-body">
                  <div class="tmpl-card-name" :title="tmpl.name">{{ tmpl.name }}</div>
                  <div class="tmpl-card-meta">
                    <span class="tmpl-card-provider">{{ tmpl.provider }}</span>
                    <span class="tmpl-card-id">{{ tmpl.id }}</span>
                  </div>
                  <div class="tmpl-card-tags">
                    <span v-for="tag in (tmpl.tags || []).slice(0,3)" :key="tag" class="tmpl-tag">{{ tag }}</span>
                  </div>

                  <!-- 上架时间信息 -->
                  <div v-if="tmpl.listing" class="tmpl-time-block">
                    <div class="tmpl-time-row">
                      <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
                      <span class="tmpl-time-val">{{ formatTmplDate(tmpl.listing.availableFrom) }}</span>
                      <span class="tmpl-time-arrow">→</span>
                      <span class="tmpl-time-val">{{ tmpl.listing.availableTo ? formatTmplDate(tmpl.listing.availableTo) : '永久' }}</span>
                    </div>
                  </div>
                  <div v-else class="tmpl-time-empty">未设置上架时间</div>
                </div>

                <!-- 操作栏 -->
                <div class="tmpl-card-actions">
                  <button
                    v-if="!tmpl.isListed"
                    class="tmpl-action-btn tmpl-action-btn--primary"
                    @click="openActivateDialog(tmpl)"
                  >
                    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="23 6 13.5 15.5 8.5 10.5 1 18"/><polyline points="17 6 23 6 23 12"/></svg>
                    上架
                  </button>
                  <button
                    v-else
                    class="tmpl-action-btn tmpl-action-btn--ghost"
                    @click="openActivateDialog(tmpl)"
                  >
                    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="3"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14M4.93 4.93a10 10 0 0 0 0 14.14"/></svg>
                    编辑时间
                  </button>
                  <button
                    v-if="tmpl.isListed"
                    class="tmpl-action-btn tmpl-action-btn--warn"
                    @click="handleDeactivate(tmpl)"
                  >
                    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                    下架
                  </button>
                  <button
                    v-if="tmpl.listing"
                    class="tmpl-action-btn tmpl-action-btn--danger"
                    @click="handleRemoveRecord(tmpl)"
                  >
                    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14H6L5 6"/><path d="M10 11v6M14 11v6"/></svg>
                    清除
                  </button>
                </div>
              </div>
            </div>
          </div><!-- end local tab -->

          <!-- ─── OfficePLUS 导入标签内容 ──────────────────────────────────── -->
          <div v-show="tmplActiveTab === 'officeplus'" class="op-import-panel">

            <!-- 步骤引导栏 -->
            <div class="op-steps-bar">
              <div class="op-step">
                <div class="op-step-num">1</div>
                <div class="op-step-body">
                  <div class="op-step-title">前往 OfficePLUS 下载模板</div>
                  <div class="op-step-sub">登录账号后下载 .pptx 文件到本地</div>
                </div>
                <a href="https://www.officeplus.cn/PPT/template/" target="_blank" class="op-step-link" @click.prevent="openOpSite">
                  <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>
                  打开网站
                </a>
              </div>
              <div class="op-step-arrow">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polyline points="9 18 15 12 9 6"/></svg>
              </div>
              <div class="op-step">
                <div class="op-step-num">2</div>
                <div class="op-step-body">
                  <div class="op-step-title">批量上传到系统</div>
                  <div class="op-step-sub">支持同时选择多个 .pptx 文件</div>
                </div>
              </div>
              <div class="op-step-arrow">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polyline points="9 18 15 12 9 6"/></svg>
              </div>
              <div class="op-step">
                <div class="op-step-num">3</div>
                <div class="op-step-body">
                  <div class="op-step-title">在本地模板库上架</div>
                  <div class="op-step-sub">切换到"本地模板库"设置上架时间</div>
                </div>
              </div>
            </div>

            <!-- 拖拽上传区 -->
            <div
              class="op-dropzone"
              :class="{
                'is-dragover': opDragover,
                'is-uploading': opBatchUploading,
                'has-files': opPendingFiles.length > 0
              }"
              @dragover.prevent="opDragover = true"
              @dragleave.prevent="opDragover = false"
              @drop.prevent="onOpDrop"
              @click="!opBatchUploading && $refs.opFileInput.click()"
            >
              <input
                ref="opFileInput"
                type="file"
                accept=".pptx"
                multiple
                style="display:none"
                @change="onOpFileSelect"
              />

              <!-- 上传进行中 -->
              <div v-if="opBatchUploading" class="op-dz-uploading">
                <svg width="36" height="36" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" class="spin op-dz-spin-icon"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"/></svg>
                <div class="op-dz-uploading-text">正在上传 {{ opBatchProgress.done }} / {{ opBatchProgress.total }} …</div>
                <div class="op-dz-progress-bar">
                  <div class="op-dz-progress-fill" :style="{ width: (opBatchProgress.total ? opBatchProgress.done / opBatchProgress.total * 100 : 0) + '%' }"></div>
                </div>
              </div>

              <!-- 有待上传文件 -->
              <div v-else-if="opPendingFiles.length > 0" class="op-dz-files">
                <div class="op-dz-files-header">
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>
                  已选择 {{ opPendingFiles.length }} 个文件
                  <button class="op-dz-clear-btn" @click.stop="opPendingFiles = []">
                    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                    清空
                  </button>
                </div>
                <div class="op-dz-file-list">
                  <div v-for="(f, i) in opPendingFiles" :key="i" class="op-dz-file-item">
                    <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" class="op-dz-file-icon"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>
                    <span class="op-dz-file-name">{{ f.name }}</span>
                    <span class="op-dz-file-size">{{ (f.size / 1024 / 1024).toFixed(1) }} MB</span>
                    <button class="op-dz-remove-btn" @click.stop="opPendingFiles.splice(i, 1)">
                      <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                    </button>
                  </div>
                </div>
                <div class="op-dz-add-more" @click.stop="$refs.opFileInput.click()">
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
                  继续添加文件
                </div>
              </div>

              <!-- 默认空状态 -->
              <div v-else class="op-dz-idle">
                <div class="op-dz-icon" :class="{ 'is-dragover': opDragover }">
                  <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><polyline points="16 16 12 12 8 16"/><line x1="12" y1="12" x2="12" y2="21"/><path d="M20.39 18.39A5 5 0 0 0 18 9h-1.26A8 8 0 1 0 3 16.3"/></svg>
                </div>
                <div class="op-dz-main-text">
                  {{ opDragover ? '释放以添加文件' : '拖拽 .pptx 文件到这里' }}
                </div>
                <div class="op-dz-sub-text">或点击此区域选择文件，支持同时选择多个</div>
              </div>
            </div>

            <!-- 上传按钮 -->
            <div v-if="opPendingFiles.length > 0 && !opBatchUploading" class="op-upload-action">
              <div class="op-upload-summary">
                共 {{ opPendingFiles.length }} 个文件 ·
                {{ (opPendingFiles.reduce((s,f) => s+f.size, 0)/1024/1024).toFixed(1) }} MB
              </div>
              <button class="op-upload-submit-btn" @click="doOpBatchUpload">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="16 16 12 12 8 16"/><line x1="12" y1="12" x2="12" y2="21"/><path d="M20.39 18.39A5 5 0 0 0 18 9h-1.26A8 8 0 1 0 3 16.3"/></svg>
                上传 {{ opPendingFiles.length }} 个模板
              </button>
            </div>

            <!-- 上传日志 -->
            <div v-if="opImportLogs.length" class="op-import-log">
              <div class="op-log-header">
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>
                上传记录
                <button class="op-log-clear-btn" @click="opImportLogs = []">清空</button>
              </div>
              <div
                v-for="(log, idx) in opImportLogs"
                :key="idx"
                class="op-log-item"
                :class="log.ok ? 'ok' : 'fail'"
              >
                <span class="op-log-dot"></span>
                <div class="op-log-content">
                  <span class="op-log-text">{{ log.text }}</span>
                  <span v-if="log.size" class="op-log-size">{{ log.size }}</span>
                </div>
              </div>
            </div>
          </div><!-- end officeplus tab -->

        </section>
      </div>
    </section>

    <!-- ── 模板上架对话框 ─────────────────────────────────────────────────── -->
    <el-dialog
      v-model="activateDialog.visible"
      :title="activateDialog.tmpl ? `上架模板：${activateDialog.tmpl.name}` : '上架模板'"
      width="460px"
      destroy-on-close
      class="tmpl-activate-dialog"
    >
      <div v-if="activateDialog.tmpl" class="activate-dialog-body">
        <!-- 模板预览 -->
        <div class="activate-tmpl-preview">
          <img
            v-if="activateDialog.tmpl && !tmplPreviewFailed.has(activateDialog.tmpl.id)"
            :src="resolveTmplPreviewUrl(activateDialog.tmpl)"
            :alt="activateDialog.tmpl.name"
            class="activate-tmpl-img"
            @error="tmplPreviewFailed.add(activateDialog.tmpl.id)"
          />
          <div v-else class="activate-tmpl-fallback">
            <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" opacity="0.3"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
          </div>
          <div class="activate-tmpl-info">
            <div class="activate-tmpl-name">{{ activateDialog.tmpl.name }}</div>
            <div class="activate-tmpl-provider">{{ activateDialog.tmpl.provider }} · {{ activateDialog.tmpl.id }}</div>
          </div>
        </div>

        <!-- 时间设置 -->
        <div class="activate-form">
          <div class="activate-field">
            <label class="activate-label">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
              上架开始时间
            </label>
            <el-date-picker
              v-model="activateDialog.from"
              type="datetime"
              placeholder="默认为当前时间（立即上架）"
              format="YYYY-MM-DD HH:mm"
              value-format="YYYY-MM-DD HH:mm:ss"
              style="width: 100%"
            />
          </div>
          <div class="activate-field">
            <label class="activate-label">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="3" y="4" width="18" height="18" rx="2"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/></svg>
              上架截止时间
              <span class="activate-label-sub">留空 = 永久</span>
            </label>
            <el-date-picker
              v-model="activateDialog.to"
              type="datetime"
              placeholder="留空则永久上架"
              format="YYYY-MM-DD HH:mm"
              value-format="YYYY-MM-DD HH:mm:ss"
              style="width: 100%"
            />
          </div>
        </div>

        <div class="activate-notice">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
          上架后，用户端模板中心将显示该模板，用户可在生成 PPT 时选用。
        </div>
      </div>
      <template #footer>
        <div class="activate-dialog-footer">
          <button class="activate-cancel-btn" @click="activateDialog.visible = false">取消</button>
          <button class="activate-confirm-btn" :disabled="activateDialog.loading" @click="confirmActivate">
            <svg v-if="!activateDialog.loading" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="23 6 13.5 15.5 8.5 10.5 1 18"/><polyline points="17 6 23 6 23 12"/></svg>
            <svg v-else width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="spin"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"/></svg>
            {{ activateDialog.loading ? '处理中…' : '确认上架' }}
          </button>
        </div>
      </template>
    </el-dialog>

    <!-- ── 素材详情抽屉 ──────────────────────────────────────────────────── -->
    <el-drawer
      v-model="materialDrawer.visible"
      :title="materialDrawer.data ? materialDrawer.data.filename : '素材详情'"
      direction="rtl"
      size="620px"
      :destroy-on-close="true"
    >
      <div class="drawer-body" v-if="materialDrawer.data">
        <!-- 基本信息 -->
        <div class="drawer-section">
          <el-descriptions :column="2" border size="small">
            <el-descriptions-item label="素材 ID" :span="2">
              <span class="mono-text">{{ materialDrawer.data.id }}</span>
            </el-descriptions-item>
            <el-descriptions-item label="文件名" :span="2">{{ materialDrawer.data.filename }}</el-descriptions-item>
            <el-descriptions-item label="类型">{{ materialDrawer.data.fileType?.toUpperCase() }}</el-descriptions-item>
            <el-descriptions-item label="大小">{{ formatBytes(materialDrawer.data.fileSize) }}</el-descriptions-item>
            <el-descriptions-item label="提取状态">
              <el-tag :type="materialStatusType(materialDrawer.data.status)" size="small">
                {{ materialStatusText(materialDrawer.data.status) }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="上传时间">
              {{ materialDrawer.data.createdAt
                ? dayjs.unix(materialDrawer.data.createdAt).format('YYYY/MM/DD HH:mm')
                : '-' }}
            </el-descriptions-item>
          </el-descriptions>
        </div>

        <!-- Tab 切换：文件预览 / AI 审核 -->
        <el-tabs v-model="materialDrawer.activeTab" class="drawer-tabs">

          <!-- ── Tab 1：文件预览 ──────────────────────────────────────── -->
          <el-tab-pane label="文件预览" name="preview">
            <div class="file-preview-toolbar">
              <el-button size="small" type="primary" plain @click="openFileInNewTab">
                在新窗口打开
              </el-button>
              <span class="file-preview-tip">
                {{ materialDrawer.data.fileType === 'pdf' ? 'PDF 支持内联预览' :
                   materialDrawer.data.fileType === 'txt' ? '文本文件可直接预览' :
                   'DOCX 文件请在新窗口下载后查看' }}
              </span>
            </div>

            <!-- PDF / TXT：iframe 内联预览 -->
            <div
              v-if="materialDrawer.data.fileType === 'pdf' || materialDrawer.data.fileType === 'txt'"
              class="file-preview-frame-wrap"
            >
              <iframe
                :src="materialDrawer.fileUrl"
                class="file-preview-frame"
                frameborder="0"
                @error="materialDrawer.previewError = true"
              />
              <div v-if="materialDrawer.previewError" class="preview-fallback">
                <p>预览加载失败，请尝试在新窗口打开。</p>
                <el-button type="primary" plain size="small" @click="openFileInNewTab">新窗口打开</el-button>
              </div>
            </div>

            <!-- DOCX 等：提示下载 -->
            <div v-else class="file-preview-fallback">
              <el-icon style="font-size: 48px; color: #c0c4cc;"><Document /></el-icon>
              <p>{{ materialDrawer.data.fileType?.toUpperCase() }} 文件不支持浏览器内联预览。</p>
              <el-button type="primary" @click="openFileInNewTab">下载 / 查看原始文件</el-button>
            </div>
          </el-tab-pane>

          <!-- ── Tab 2：AI 内容审核 ───────────────────────────────────── -->
          <el-tab-pane label="AI 内容审核" name="review">
            <div class="drawer-section-header" style="margin-bottom: 12px;">
              <el-button
                type="primary"
                size="small"
                :loading="materialDrawer.reviewing"
                :disabled="materialDrawer.data.status !== 'completed'"
                @click="triggerReview"
              >
                {{ materialDrawer.reviewResult ? '重新审核' : '发起审核' }}
              </el-button>
            </div>

            <div v-if="materialDrawer.data.status !== 'completed'" class="review-hint warning">
              素材尚未完成文本提取，暂无法进行 AI 审核。
            </div>
            <div v-else-if="materialDrawer.reviewing" class="review-hint info">
              正在调用 AI 分析中，请稍候…
            </div>
            <div v-else-if="materialDrawer.reviewResult" class="review-result-card"
                 :class="materialDrawer.reviewResult.result">
              <div class="review-badge">
                <span v-if="materialDrawer.reviewResult.result === 'pass'" class="badge-pass">✓ 内容合规</span>
                <span v-else-if="materialDrawer.reviewResult.result === 'violation'" class="badge-violation">✕ 存在违规</span>
                <span v-else class="badge-unknown">? 无法判断</span>
              </div>
              <p class="review-reason">{{ materialDrawer.reviewResult.reason }}</p>
              <p class="review-time" v-if="materialDrawer.reviewResult.reviewedAt">
                审核时间：{{ dayjs.unix(materialDrawer.reviewResult.reviewedAt).format('YYYY/MM/DD HH:mm:ss') }}
              </p>
              <div v-if="materialDrawer.reviewResult.result === 'violation'" class="review-action">
                <el-button type="danger" size="small" @click="deleteFromDrawer">
                  确认违规并删除该素材
                </el-button>
              </div>
            </div>
            <div v-else class="review-hint">
              点击"发起审核"，将使用 AI 大模型对素材内容进行违规检测。
            </div>

            <!-- 提取内容（辅助审核参考） -->
            <template v-if="materialDrawer.data.extractResult">
              <div style="margin-top: 16px;">
                <div style="font-size: 13px; color: #909399; margin-bottom: 6px;">提取文本内容（审核依据）</div>
                <div class="content-preview">
                  <pre class="content-text">{{ formatExtractContent(materialDrawer.data.extractResult) }}</pre>
                </div>
              </div>
            </template>
          </el-tab-pane>

        </el-tabs>
      </div>

      <div v-else class="drawer-loading">
        <el-skeleton :rows="8" animated />
      </div>
    </el-drawer>
  </div>
</template>

<script setup>
import { computed, nextTick, onMounted, onUnmounted, reactive, ref, watch } from 'vue'
import { useStore } from 'vuex'
import { useRouter } from 'vue-router'
import dayjs from 'dayjs'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  ArrowLeft,
  ArrowRight,
  Bell,
  Check,
  Close,
  DataLine,
  Download,
  Document,
  Histogram,
  Monitor,
  PieChart,
  Plus,
  Refresh,
  Search,
  Setting,
  TrendCharts,
  UserFilled,
  FolderOpened
} from '@element-plus/icons-vue'
import ElChart from '@/components/ElChart.vue'
import adminAPI from '@/api/admin'

const store = useStore()
const router = useRouter()

const activeNav = ref('overview')
const adminSidebarCollapsed = ref(false)
const timeRange = ref('week')
const searchQuery = ref('')
const statusFilter = ref('all')
const recordDateRange = ref([])
const currentPage = ref(1)
const pageSize = ref(6)
const chartRefs = ref({})
const dragState = reactive({ draggingId: null })
const metricsLoading = ref(false)
const usersLoading = ref(false)
const users = ref([])
const userSelection = ref([])   // 批量勾选
const userSearchQuery = ref('')
const userStatusFilter = ref('all')
const userPage = ref(1)
const userPageSize = ref(8)
const metricsState = reactive({
  summary: {
    total: 0,
    success: 0,
    failed: 0,
    successRate: 0,
    uniqueUsers: 0,
    templateCount: 0
  },
  activity: { labels: [], values: [] },
  generation: { labels: [], series: [] },
  templateShare: [],
  successRate: { success: 0, failed: 0 },
  region: { labels: [], values: [] },
  moduleHeat: { labels: [], values: [] }
})

// ── 公告管理状态 ────────────────────────────────────────────────────────────
const announcements = ref([])
const announcementsLoading = ref(false)
const announcementTotal = ref(0)
const announcementPage = ref(1)
const announcementPageSize = ref(20)

const annDialog = reactive({
  visible: false,
  isEdit: false,
  saving: false,
  editId: null,
  form: { title: '', content: '', is_pinned: false, starts_at: '', expires_at: '' }
})

// ── 偏好洞察状态 ────────────────────────────────────────────────────────────
const insightsLoading = ref(false)
const insightsData = ref(null)   // raw API response
/** 任意洞察数据仍在加载中 —— 驱动统一骨架屏 */
const insightsBusy = computed(() => insightsLoading.value || metricsLoading.value)
const wordCloudCanvas = ref(null)

// ── 操作审计日志状态 ─────────────────────────────────────────────────────────
const auditLogs = ref([])
const auditLoading = ref(false)
const auditTotal = ref(0)
const auditPage = ref(1)
const auditPageSize = ref(30)
const auditFilter = reactive({ action: '', dateRange: [], keyword: '' })

// ── 系统配置中心状态 ──────────────────────────────────────────────────────────
const settingsItems = ref([])          // 从后端取到的全量配置项（含元数据）
const settingsDraft = ref({})          // 当前编辑草稿（key → 已转换的 js 值）
const settingsOriginal = ref({})       // 加载时的原始值（用于撤销）
const settingsLoading = ref(false)
const settingsSaving = ref(false)
const settingsLastSaved = ref('')
const settingsActiveTab = ref('basic')

const settingsTabs = [
  {
    id: 'basic',
    label: '基本配置',
    icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 1v4M12 19v4M4.22 4.22l2.83 2.83M16.95 16.95l2.83 2.83M1 12h4M19 12h4M4.22 19.78l2.83-2.83M16.95 7.05l2.83-2.83"/></svg>'
  },
  {
    id: 'limits',
    label: '生成限制',
    icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>'
  },
  {
    id: 'ai',
    label: 'AI 模型',
    icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2a10 10 0 1 0 0 20A10 10 0 0 0 12 2z"/><path d="M8 14s1.5 2 4 2 4-2 4-2"/><line x1="9" y1="9" x2="9.01" y2="9"/><line x1="15" y1="9" x2="15.01" y2="9"/></svg>'
  }
]

const settingsGrouped = computed(() => {
  const groups = {}
  for (const item of settingsItems.value) {
    const g = item.group || 'basic'
    if (!groups[g]) groups[g] = []
    groups[g].push(item)
  }
  return groups
})

const auditActionOptions = [
  { value: 'disable_user',          label: '禁用用户' },
  { value: 'enable_user',           label: '启用用户' },
  { value: 'batch_disable_user',    label: '批量禁用用户' },
  { value: 'batch_enable_user',     label: '批量启用用户' },
  { value: 'delete_material',       label: '删除素材' },
  { value: 'batch_delete_material', label: '批量删除素材' },
  { value: 'create_announcement',   label: '创建公告' },
  { value: 'update_announcement',   label: '更新公告' },
  { value: 'delete_announcement',   label: '删除公告' }
]

// ── 素材管理状态 ────────────────────────────────────────────────────────────
const materials = ref([])
const materialsLoading = ref(false)
const materialTotal = ref(0)
const materialPage = ref(1)
const materialPageSize = ref(15)
const materialSelection = ref([])
const materialSearch = reactive({ userId: '', status: '', fileType: '' })
const materialStats = reactive({ total: -1, totalSize: 0, completed: 0, pending: 0, failed: 0 })

// ── 素材详情抽屉状态 ────────────────────────────────────────────────────────
const materialDrawer = reactive({
  visible: false,
  data: null,          // 从 /admin/materials/content 获取的完整数据
  reviewing: false,
  reviewResult: null,  // { result, reason, reviewedAt }
  activeTab: 'preview',
  fileUrl: '',         // 带 token 的原始文件 URL
  previewError: false
})

const navItems = [
  { id: 'overview', label: '运营总览', icon: DataLine },
  { id: 'records', label: '生成记录', icon: Document, badge: 'NEW' },
  { id: 'users', label: '用户管理', icon: UserFilled },
  { id: 'materials', label: '素材管理', icon: FolderOpened },
  { id: 'announcements', label: '公告管理', icon: Bell },
  { id: 'insights', label: '偏好洞察', icon: PieChart },
  { id: 'audit', label: '操作日志', icon: Histogram },
  { id: 'templates', label: '模板管理', icon: Monitor },
  { id: 'settings', label: '系统设置', icon: Setting }
]

const chartTypeLabels = {
  bar: '柱状图',
  line: '折线图',
  pie: '饼图'
}

const chartCards = ref([
  {
    id: 'activity',
    title: '用户活跃度',
    subtitle: '每日/每周/每月登录次数',
    chartType: 'line',
    types: ['line', 'bar'],
    height: 320,
    footnote: '基于生成记录统计活跃度'
  },
  {
    id: 'generation',
    title: '生成频率',
    subtitle: '按时间段/模板类型分类',
    chartType: 'bar',
    types: ['bar', 'line'],
    height: 320,
    footnote: '结合时间段与模板使用统计'
  },
  {
    id: 'templateShare',
    title: '模板偏好分布',
    subtitle: '热门模板使用占比',
    chartType: 'pie',
    types: ['pie', 'bar'],
    height: 320,
    footnote: '统计模板使用占比'
  },
  {
    id: 'successRate',
    title: '生成成功率',
    subtitle: '成功/失败占比统计',
    chartType: 'pie',
    types: ['pie', 'bar'],
    height: 300,
    footnote: '成功率 = 成功任务 / 总任务'
  }
])
const defaultChartCards = chartCards.value.map((card) => ({ ...card }))

const currentUser = computed(() => store.getters.currentUser)
const adminHistory = computed(() => store.getters.adminHistory || [])
const adminHistoryLoading = computed(() => store.getters.adminHistoryLoading)

const userInitials = computed(() => {
  const name = currentUser.value?.username || 'A'
  return name.slice(0, 2).toUpperCase()
})

const lastUpdated = computed(() => dayjs().format('YYYY/MM/DD HH:mm'))

const normalizedHistory = computed(() => adminHistory.value || [])

const isFailureStatus = (status) => {
  const value = (status || '').toLowerCase()
  // 仅 completed 为成功，其余（pending/queued/processing/failed/error/...）均非成功
  return value !== 'completed'
}

const successCount = computed(() => metricsState.successRate.success || 0)
const failureCount = computed(() => metricsState.successRate.failed || 0)
const totalCount = computed(() => metricsState.summary.total || 0)
const successRate = computed(() => metricsState.summary.successRate || 0)
const uniqueUsers = computed(() => metricsState.summary.uniqueUsers || 0)
const templateCount = computed(() => metricsState.summary.templateCount || 0)

const activityLabels = computed(() => metricsState.activity.labels || [])
const activityValues = computed(() => metricsState.activity.values || [])
const generationLabels = computed(() => metricsState.generation.labels || ['上午', '下午', '傍晚', '深夜', '清晨'])
const generationSeries = computed(() => metricsState.generation.series || [])
const templateShareData = computed(() => metricsState.templateShare || [])

const chartOptions = computed(() => {
  const options = {}
  chartCards.value.forEach((card) => {
    options[card.id] = buildOption(card)
  })
  return options
})

const palette = ['#0EA5E9', '#10b981', '#f97316', '#38BDF8', '#0284C7', '#0ea5e9']

const axisStyle = {
  axisLine: { lineStyle: { color: 'rgba(148,163,184,0.5)' } },
  axisTick: { show: false },
  axisLabel: { color: '#64748b', fontSize: 12 }
}

const gridBase = { left: 28, right: 20, top: 40, bottom: 30, containLabel: true }

const buildLineBarOption = (labels, series, type, name) => ({
  color: palette,
  tooltip: { trigger: 'axis' },
  grid: gridBase,
  xAxis: {
    type: 'category',
    data: labels,
    ...axisStyle
  },
  yAxis: {
    type: 'value',
    ...axisStyle,
    splitLine: { lineStyle: { color: 'rgba(148,163,184,0.2)' } }
  },
  series: [
    {
      name,
      type,
      data: series,
      smooth: type === 'line',
      areaStyle: type === 'line' ? { color: 'rgba(37,99,235,0.15)' } : undefined,
      itemStyle: { borderRadius: 6 }
    }
  ]
})

const buildMultiSeriesOption = (labels, seriesList, type) => ({
  color: palette,
  tooltip: { trigger: 'axis' },
  legend: {
    top: 8,
    textStyle: { color: '#475569' }
  },
  grid: gridBase,
  xAxis: { type: 'category', data: labels, ...axisStyle },
  yAxis: {
    type: 'value',
    ...axisStyle,
    splitLine: { lineStyle: { color: 'rgba(148,163,184,0.2)' } }
  },
  series: seriesList.map((item) => ({
    name: item.name,
    type,
    data: item.data,
    smooth: type === 'line',
    areaStyle: type === 'line' ? { opacity: 0.12 } : undefined,
    itemStyle: { borderRadius: 6 }
  }))
})

const buildPieOption = (data) => ({
  color: palette,
  tooltip: { trigger: 'item' },
  legend: {
    bottom: 0,
    textStyle: { color: '#475569' }
  },
  series: [
    {
      type: 'pie',
      radius: ['40%', '70%'],
      data,
      label: { color: '#0f172a' },
      itemStyle: { borderRadius: 8, borderColor: '#ffffff', borderWidth: 2 }
    }
  ]
})

const buildOption = (card) => {
  if (card.id === 'activity') {
    return buildLineBarOption(
      activityLabels.value,
      activityValues.value,
      card.chartType,
      '活跃次数'
    )
  }
  if (card.id === 'generation') {
    return buildMultiSeriesOption(
      generationLabels.value,
      generationSeries.value.map((item) => ({ name: item.name, data: item.values || [] })),
      card.chartType
    )
  }
  if (card.id === 'templateShare') {
    if (card.chartType === 'pie') {
      return buildPieOption(templateShareData.value)
    }
    return buildLineBarOption(
      templateShareData.value.map((item) => item.name),
      templateShareData.value.map((item) => item.value),
      card.chartType,
      '模板使用次数'
    )
  }
  if (card.id === 'successRate') {
    const data = [
      { value: successCount.value || 0, name: '成功' },
      { value: failureCount.value || 0, name: '失败' }
    ]
    if (card.chartType === 'pie') {
      return buildPieOption(data)
    }
    return buildLineBarOption(['成功', '失败'], [successCount.value, failureCount.value], 'bar', '任务量')
  }
  return {}
}

const rangeLabel = computed(() => {
  if (timeRange.value === 'day') return '最近 24 小时'
  if (timeRange.value === 'month') return '最近 30 天'
  return '最近 7 天'
})

const statCards = computed(() => [
  {
    id: 'total',
    label: '区间生成总量',
    value: totalCount.value || 0,
    note: `统计口径：${rangeLabel.value}`,
    icon: Histogram
  },
  {
    id: 'success',
    label: '成功率',
    value: `${successRate.value}%`,
    note: `${successCount.value} 成功 / ${failureCount.value} 失败`,
    icon: PieChart
  },
  {
    id: 'active',
    label: '活跃用户',
    value: uniqueUsers.value,
    note: `${rangeLabel.value}内去重用户数`,
    icon: UserFilled
  },
  {
    id: 'templates',
    label: '涉及模板数',
    value: templateCount.value || 0,
    note: `${rangeLabel.value}内有调用的模板种数`,
    icon: TrendCharts
  }
])


const filteredHistory = computed(() => {
  let items = normalizedHistory.value
  const keyword = searchQuery.value.trim().toLowerCase()
  if (keyword) {
    items = items.filter((item) => {
      const title = item.title?.toLowerCase() || ''
      const topic = item.topic?.toLowerCase() || ''
      const user = item.userName?.toLowerCase() || ''
      return title.includes(keyword) || topic.includes(keyword) || user.includes(keyword)
    })
  }
  if (statusFilter.value !== 'all') {
    items = items.filter((item) => {
      const s = (item.status || '').toLowerCase()
      if (statusFilter.value === 'completed') return s === 'completed'
      if (statusFilter.value === 'failed') return s === 'failed'
      if (statusFilter.value === 'pending') return s === 'pending' || s === 'queued' || s === 'processing'
      return true
    })
  }
  if (recordDateRange.value?.length === 2) {
    const [start, end] = recordDateRange.value
    const startDate = dayjs(start)
    const endDate = dayjs(end).endOf('day')
    items = items.filter((item) => {
      if (!item.createdAt) return true
      const created = dayjs(item.createdAt)
      return created.isAfter(startDate) && created.isBefore(endDate)
    })
  }
  return items
})

const pagedHistory = computed(() => {
  const start = (currentPage.value - 1) * pageSize.value
  return filteredHistory.value.slice(start, start + pageSize.value)
})

const totalPages = computed(() => Math.max(1, Math.ceil(filteredHistory.value.length / pageSize.value)))

const userTotalPages = computed(() => Math.max(1, Math.ceil(filteredUsers.value.length / userPageSize.value)))

const userPaginationPages = computed(() => {
  const total = userTotalPages.value
  const cur = userPage.value
  if (total <= 7) return Array.from({ length: total }, (_, i) => i + 1)
  const pages = []
  if (cur <= 4) {
    pages.push(1, 2, 3, 4, 5, '…', total)
  } else if (cur >= total - 3) {
    pages.push(1, '…', total - 4, total - 3, total - 2, total - 1, total)
  } else {
    pages.push(1, '…', cur - 1, cur, cur + 1, '…', total)
  }
  return pages
})

const paginationPages = computed(() => {
  const total = totalPages.value
  const cur = currentPage.value
  if (total <= 7) return Array.from({ length: total }, (_, i) => i + 1)
  const pages = []
  if (cur <= 4) {
    pages.push(1, 2, 3, 4, 5, '…', total)
  } else if (cur >= total - 3) {
    pages.push(1, '…', total - 4, total - 3, total - 2, total - 1, total)
  } else {
    pages.push(1, '…', cur - 1, cur, cur + 1, '…', total)
  }
  return pages
})

const filteredUsers = computed(() => {
  let items = users.value
  const keyword = userSearchQuery.value.trim().toLowerCase()
  if (keyword) {
    items = items.filter((item) => {
      const name = item.username?.toLowerCase() || ''
      const email = item.email?.toLowerCase() || ''
      return name.includes(keyword) || email.includes(keyword)
    })
  }
  if (userStatusFilter.value !== 'all') {
    items = items.filter((item) =>
      userStatusFilter.value === 'disabled' ? item.isDisabled : !item.isDisabled
    )
  }
  return items
})

const pagedUsers = computed(() => {
  const start = (userPage.value - 1) * userPageSize.value
  return filteredUsers.value.slice(start, start + userPageSize.value)
})

const setChartRef = (id, el) => {
  if (el) {
    chartRefs.value[id] = el
  }
}

const resizeCharts = () => {
  nextTick(() => {
    Object.values(chartRefs.value).forEach((chart) => {
      chart?.refresh?.()
      chart?.getInstance?.()?.resize()
    })
  })
}

const onDragStart = (id) => {
  dragState.draggingId = id
}

const onDragEnd = () => {
  dragState.draggingId = null
}

const onDrop = (targetId) => {
  if (!dragState.draggingId || dragState.draggingId === targetId) {
    return
  }
  const cards = [...chartCards.value]
  const fromIndex = cards.findIndex((card) => card.id === dragState.draggingId)
  const toIndex = cards.findIndex((card) => card.id === targetId)
  const [moved] = cards.splice(fromIndex, 1)
  cards.splice(toIndex, 0, moved)
  chartCards.value = cards
  dragState.draggingId = null
}

const resetDashboard = () => {
  chartCards.value = defaultChartCards.map((card) => ({ ...card }))
}

const exportCard = (id, format) => {
  const chart = chartRefs.value[id]
  if (!chart) return
  const imageUrl = chart.getDataURL('png')
  if (!imageUrl) return
  if (format === 'pdf') {
    const win = window.open('', '_blank')
    if (win) {
      win.document.write(`<img src="${imageUrl}" style="width:100%;"/>`)
      win.document.close()
      win.focus()
      win.print()
    }
    return
  }
  const link = document.createElement('a')
  link.href = imageUrl
  link.download = `chart-${id}-${dayjs().format('YYYYMMDD-HHmm')}.png`
  link.click()
}

const exportDashboard = () => {
  const cards = chartCards.value.map((card) => card.id)
  cards.forEach((id, index) => {
    setTimeout(() => exportCard(id, 'png'), index * 250)
  })
}

const applyMetrics = (data = {}) => {
  if (data.summary) {
    Object.assign(metricsState.summary, data.summary)
  }
  if (data.activity) {
    metricsState.activity.labels = data.activity.labels || []
    metricsState.activity.values = data.activity.values || []
  }
  if (data.generation) {
    metricsState.generation.labels = data.generation.labels || metricsState.generation.labels
    metricsState.generation.series = data.generation.series || []
  }
  metricsState.templateShare = data.templateShare || []
  metricsState.successRate = data.successRate || { success: 0, failed: 0 }
  if (data.region) {
    metricsState.region.labels = data.region.labels || []
    metricsState.region.values = data.region.values || []
  }
  if (data.moduleHeat) {
    metricsState.moduleHeat.labels = data.moduleHeat.labels || []
    metricsState.moduleHeat.values = data.moduleHeat.values || []
  }
}

const loadMetrics = async () => {
  metricsLoading.value = true
  try {
    const response = await adminAPI.metrics({ range: timeRange.value })
    applyMetrics(response.data || {})
  } catch (error) {
    // 拦截器已统一 ElMessage
    console.error('获取后台统计失败:', error?.userMessage || error)
  } finally {
    metricsLoading.value = false
  }
}

const loadUsers = async () => {
  usersLoading.value = true
  try {
    const response = await adminAPI.users()
    users.value = response.data?.items || []
  } catch (error) {
    // 拦截器已统一 ElMessage
    console.error('获取用户列表失败:', error?.userMessage || error)
  } finally {
    usersLoading.value = false
  }
}

const toggleUserStatus = async (user) => {
  if (!user?.id || user.id === currentUser.value?.id) {
    return
  }
  const nextDisabled = !user.isDisabled
  try {
    await ElMessageBox.confirm(
      nextDisabled ? `确定禁用用户 ${user.username || user.email}？` : `确定启用用户 ${user.username || user.email}？`,
      '操作确认',
      { confirmButtonText: '确认', cancelButtonText: '取消', type: 'warning' }
    )
    await adminAPI.updateUserStatus({ userId: user.id, disabled: nextDisabled })
    user.isDisabled = nextDisabled
    ElMessage.success(nextDisabled ? '用户已禁用' : '用户已启用')
  } catch (error) {
    if (error !== 'cancel' && error !== 'close') {
      console.error('更新用户状态失败:', error?.userMessage || error?.response?.data?.message)
    }
  }
}

const batchUserStatus = async (disabled) => {
  const targets = userSelection.value.filter((u) => u.id !== currentUser.value?.id)
  if (!targets.length) {
    ElMessage.warning('没有可操作的用户（当前账号不可操作自身）')
    return
  }
  const actionText = disabled ? '禁用' : '启用'
  try {
    await ElMessageBox.confirm(
      `确定批量${actionText}已选中的 ${targets.length} 位用户？`,
      `批量${actionText}确认`,
      { confirmButtonText: '确认', cancelButtonText: '取消', type: 'warning' }
    )
    const ids = targets.map((u) => u.id)
    const res = await adminAPI.batchUpdateUserStatus(ids, disabled)
    const { success = 0, skipped = 0 } = res.data || {}
    // 同步本地状态，避免重新拉取
    for (const u of targets) {
      u.isDisabled = disabled
    }
    userSelection.value = []
    ElMessage.success(`批量${actionText}完成：成功 ${success} 人，跳过 ${skipped} 人`)
  } catch (e) {
    if (e !== 'cancel' && e !== 'close') {
      console.error('批量操作失败:', e?.userMessage || e)
    }
  }
}

const refreshData = () => {
  // 根据当前激活的 tab 刷新对应模块数据
  switch (activeNav.value) {
    case 'overview':
      loadMetrics()
      break
    case 'records':
      store.dispatch('fetchAdminHistory').catch(() => {})
      break
    case 'users':
      loadUsers()
      break
    case 'materials':
      loadMaterials(materialPage.value)
      loadMaterialStats()
      break
    case 'announcements':
      loadAnnouncements(announcementPage.value)
      break
    case 'insights':
      loadInsights()
      loadMetrics()
      break
    case 'audit':
      loadAuditLogs(auditPage.value)
      break
    case 'settings':
      if (settingsItems.value.length === 0 && !settingsLoading.value) {
        loadSettings()
      }
      break
    default:
      store.dispatch('fetchAdminHistory').catch(() => {})
      loadMetrics()
  }
}

const statusText = (status) => {
  const s = (status || '').toLowerCase()
  if (s === 'completed') return '已完成'
  if (s === 'failed') return '失败'
  if (s === 'pending') return '待处理'
  if (s === 'queued') return '排队中'
  if (s === 'processing') return '生成中'
  return status || '-'
}
const statusTagType = (status) => {
  const s = (status || '').toLowerCase()
  if (s === 'completed') return 'success'
  if (s === 'failed') return 'danger'
  return 'warning'
}
const formatDate = (value) => {
  if (!value) return '-'
  const raw = String(value).trim()
  if (/^\d+$/.test(raw)) {
    const numeric = Number(raw)
    const ts = numeric < 1e12 ? numeric * 1000 : numeric
    return dayjs(ts).format('YYYY/MM/DD HH:mm')
  }
  const parsed = dayjs(raw)
  if (parsed.isValid()) {
    const year = parsed.year()
    if (year > 3000) {
      return raw.replace(/-/g, '/').replace('T', ' ').replace('Z', '').slice(0, 16)
    }
    return parsed.format('YYYY/MM/DD HH:mm')
  }
  if (/^\d{4}-\d{2}-\d{2}\s\d{2}:\d{2}/.test(raw)) {
    return raw.replace(/-/g, '/').slice(0, 16)
  }
  return raw
}

watch([searchQuery, statusFilter, recordDateRange], () => {
  currentPage.value = 1
})

watch([userSearchQuery, userStatusFilter], () => {
  userPage.value = 1
})

watch(timeRange, () => {
  loadMetrics()
})

// ── 素材管理方法 ────────────────────────────────────────────────────────────
const formatBytes = (bytes) => {
  if (!bytes || bytes === 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  let i = 0
  let val = Number(bytes)
  while (val >= 1024 && i < units.length - 1) { val /= 1024; i++ }
  return val.toFixed(i === 0 ? 0 : 1) + ' ' + units[i]
}

const materialStatusText = (status) => {
  const map = { completed: '已完成', pending: '等待中', extracting: '提取中', failed: '失败' }
  return map[status] || status
}

const materialStatusType = (status) => {
  const map = { completed: 'success', pending: 'warning', extracting: 'info', failed: 'danger' }
  return map[status] || 'info'
}

const loadMaterials = async (page = materialPage.value) => {
  materialsLoading.value = true
  materialPage.value = page
  try {
    const params = { page, page_size: materialPageSize.value }
    if (materialSearch.userId) params.user_id = materialSearch.userId
    if (materialSearch.status) params.status = materialSearch.status
    if (materialSearch.fileType) params.file_type = materialSearch.fileType
    const res = await adminAPI.materials(params)
    materials.value = res.data?.items || []
    materialTotal.value = res.data?.total || 0
  } catch (e) {
    console.error('获取素材列表失败:', e?.userMessage || e)
  } finally {
    materialsLoading.value = false
  }
}

const loadMaterialStats = async () => {
  try {
    const res = await adminAPI.materialStats()
    Object.assign(materialStats, res.data || {})
  } catch (e) {
    console.error('获取素材统计失败:', e?.userMessage || e)
  }
}

// ── 公告管理方法 ────────────────────────────────────────────────────────────
const loadAnnouncements = async (page = announcementPage.value) => {
  announcementsLoading.value = true
  announcementPage.value = page
  try {
    const res = await adminAPI.announcements({ page, page_size: announcementPageSize.value })
    announcements.value = res.data?.items || []
    announcementTotal.value = res.data?.total || 0
  } catch (e) {
    console.error('获取公告列表失败:', e?.userMessage || e)
  } finally {
    announcementsLoading.value = false
  }
}

const announcementStatusText = (row) => {
  const now = Math.floor(Date.now() / 1000)
  if (row.starts_at > now) return '未生效'
  if (row.expires_at && row.expires_at < now) return '已过期'
  return '生效中'
}

const announcementStatusType = (row) => {
  const now = Math.floor(Date.now() / 1000)
  if (row.starts_at > now) return 'info'
  if (row.expires_at && row.expires_at < now) return 'danger'
  return 'success'
}

const openAnnouncementDialog = (row = null) => {
  annDialog.isEdit = !!row
  annDialog.editId = row?.id || null
  annDialog.saving = false
  if (row) {
    annDialog.form.title = row.title
    annDialog.form.content = row.content
    annDialog.form.is_pinned = row.is_pinned
    // starts_at / expires_at 来自后端 unix timestamp，转换为 datetime-local 字符串
    annDialog.form.starts_at = row.starts_at
      ? dayjs.unix(row.starts_at).format('YYYY-MM-DD HH:mm:ss') : ''
    annDialog.form.expires_at = row.expires_at
      ? dayjs.unix(row.expires_at).format('YYYY-MM-DD HH:mm:ss') : ''
  } else {
    annDialog.form.title = ''
    annDialog.form.content = ''
    annDialog.form.is_pinned = false
    annDialog.form.starts_at = ''
    annDialog.form.expires_at = ''
  }
  annDialog.visible = true
}

const submitAnnouncement = async () => {
  if (!annDialog.form.title.trim()) {
    ElMessage.warning('请填写公告标题')
    return
  }
  if (!annDialog.form.content.trim()) {
    ElMessage.warning('请填写公告内容')
    return
  }
  annDialog.saving = true
  try {
    const payload = {
      title: annDialog.form.title.trim(),
      content: annDialog.form.content.trim(),
      is_pinned: annDialog.form.is_pinned,
      starts_at: annDialog.form.starts_at || '',
      expires_at: annDialog.form.expires_at || ''
    }
    if (annDialog.isEdit) {
      await adminAPI.updateAnnouncement(annDialog.editId, payload)
      ElMessage.success('公告已更新')
    } else {
      await adminAPI.createAnnouncement(payload)
      ElMessage.success('公告已发布')
    }
    annDialog.visible = false
    await loadAnnouncements(1)
  } catch (e) {
    console.error('提交公告失败:', e?.userMessage || e)
  } finally {
    annDialog.saving = false
  }
}

const deleteAnnouncement = async (row) => {
  try {
    await ElMessageBox.confirm(`确定删除公告「${row.title}」？`, '删除确认', {
      confirmButtonText: '确认删除',
      cancelButtonText: '取消',
      type: 'warning'
    })
    await adminAPI.deleteAnnouncement(row.id)
    ElMessage.success('公告已删除')
    await loadAnnouncements(1)
  } catch (e) {
    if (e !== 'cancel' && e !== 'close') {
      console.error('删除公告失败:', e?.userMessage || e)
    }
  }
}

// ── 偏好洞察方法 + 图表 computed ────────────────────────────────────────────
const loadInsights = async () => {
  insightsLoading.value = true
  try {
    const res = await adminAPI.insights()
    insightsData.value = res.data || null
  } catch (e) {
    console.error('获取偏好洞察失败:', e?.userMessage || e)
  } finally {
    insightsLoading.value = false
  }
}

/** 统一刷新：洞察 + 数据看板，由 Hero 刷新按钮调用 */
const refreshInsights = () => {
  loadInsights()
  loadMetrics()
}

// ── 操作审计日志方法 ─────────────────────────────────────────────────────────
const loadAuditLogs = async (page = auditPage.value) => {
  auditLoading.value = true
  auditPage.value = page
  const params = { page, page_size: auditPageSize.value }
  if (auditFilter.action) params.action = auditFilter.action
  if (auditFilter.keyword) params.q = auditFilter.keyword
  if (auditFilter.dateRange && auditFilter.dateRange.length === 2) {
    params.start = auditFilter.dateRange[0]
    params.end   = auditFilter.dateRange[1]
  }
  try {
    const res = await adminAPI.auditLogs(params)
    auditLogs.value  = res.data?.items  || []
    auditTotal.value = res.data?.total   || 0
  } catch (e) {
    ElMessage.error('获取审计日志失败: ' + (e?.userMessage || '请求异常'))
  } finally {
    auditLoading.value = false
  }
}

// ── 系统配置中心方法 ──────────────────────────────────────────────────────────

/** 将后端字符串值按 type 转换为 JS 值用于控件绑定 */
const parseSettingValue = (value, type) => {
  if (type === 'bool') return value === 'true' || value === '1'
  if (type === 'int') {
    const n = parseInt(value, 10)
    return isNaN(n) ? 0 : n
  }
  return value
}

const loadSettings = async () => {
  settingsLoading.value = true
  try {
    const res = await adminAPI.getSettings()
    const items = res.data?.items || []
    settingsItems.value = items
    const draft = {}
    const original = {}
    for (const item of items) {
      const v = parseSettingValue(item.value, item.type)
      draft[item.key] = v
      original[item.key] = v
    }
    settingsDraft.value = draft
    settingsOriginal.value = original
  } catch (e) {
    ElMessage.error('获取系统配置失败: ' + (e?.userMessage || '请求异常'))
  } finally {
    settingsLoading.value = false
  }
}

const resetDraft = () => {
  settingsDraft.value = { ...settingsOriginal.value }
  ElMessage.info('已撤销未保存的更改')
}

const saveSettings = async () => {
  settingsSaving.value = true
  try {
    // 将 JS 值转换为字符串/bool/int 发送
    const payload = {}
    for (const item of settingsItems.value) {
      const v = settingsDraft.value[item.key]
      if (item.type === 'bool') {
        payload[item.key] = !!v
      } else if (item.type === 'int') {
        payload[item.key] = Number(v) || 0
      } else {
        payload[item.key] = String(v || '')
      }
    }
    await adminAPI.updateSettings(payload)
    // 同步 original
    settingsOriginal.value = { ...settingsDraft.value }
    const now = new Date()
    settingsLastSaved.value = now.toLocaleString('zh-CN')
    ElMessage.success('配置已保存，热更新生效')
  } catch (e) {
    ElMessage.error('保存失败: ' + (e?.userMessage || e?.message || '请求异常'))
  } finally {
    settingsSaving.value = false
  }
}

const exportAuditCSV = () => {
  const params = {}
  if (auditFilter.action) params.action = auditFilter.action
  if (auditFilter.keyword) params.q = auditFilter.keyword
  if (auditFilter.dateRange && auditFilter.dateRange.length === 2) {
    params.start = auditFilter.dateRange[0]
    params.end   = auditFilter.dateRange[1]
  }
  const url = adminAPI.exportAuditLogs(params)
  window.open(url, '_blank')
}

const exportPptHistoryCSV = () => {
  const params = {}
  if (searchQuery.value) params.q = searchQuery.value
  window.open(adminAPI.exportPptHistory(params), '_blank')
}

// ── AI 索引管理 ────────────────────────────────────────────────────────────
const aiIndexAvailable = ref(false)
const aiIndexRunning   = ref(false)
const aiIndexedCount   = ref(0)
let aiIndexPollTimer   = null

const fetchIndexStatus = async () => {
  try {
    const res = await adminAPI.indexStatus()
    const data = res.data || {}
    aiIndexAvailable.value = data.vector_available ?? false
    aiIndexRunning.value   = data.running ?? false
    aiIndexedCount.value   = data.indexed_count ?? 0
  } catch {
    aiIndexAvailable.value = false
  }
}

const handleReindex = async () => {
  if (aiIndexRunning.value) return
  try {
    await adminAPI.reindexPpt()
    aiIndexRunning.value = true
    ElMessage.success('AI 索引重建任务已启动，正在后台处理…')
    // 轮询直到 running=false
    aiIndexPollTimer = setInterval(async () => {
      await fetchIndexStatus()
      if (!aiIndexRunning.value) {
        clearInterval(aiIndexPollTimer)
        aiIndexPollTimer = null
        ElMessage.success(`AI 索引重建完成，共索引 ${aiIndexedCount.value} 条记录`)
      }
    }, 3000)
  } catch (e) {
    const msg = e?.response?.data?.message || 'AI 索引重建启动失败'
    ElMessage.error(msg)
  }
}

// 进入生成记录页时拉取一次索引状态
watch(
  () => activeNav.value,
  (nav) => {
    if (nav === 'records') fetchIndexStatus()
  },
  { immediate: true }
)

onUnmounted(() => {
  if (aiIndexPollTimer) clearInterval(aiIndexPollTimer)
})

const exportUsersCSV = () => {
  const params = {}
  if (userSearchQuery.value) params.q = userSearchQuery.value
  window.open(adminAPI.exportUsers(params), '_blank')
}

const auditActionLabel = (action) => {
  const map = {
    disable_user:          '禁用用户',
    enable_user:           '启用用户',
    batch_disable_user:    '批量禁用',
    batch_enable_user:     '批量启用',
    delete_material:       '删除素材',
    batch_delete_material: '批量删素材',
    create_announcement:   '创建公告',
    update_announcement:   '更新公告',
    delete_announcement:   '删除公告'
  }
  return map[action] || action
}

const auditActionTagType = (action) => {
  if (action.startsWith('delete') || action.startsWith('batch_delete')) return 'danger'
  if (action.startsWith('disable') || action.startsWith('batch_disable')) return 'warning'
  if (action.startsWith('enable')  || action.startsWith('batch_enable'))  return 'success'
  if (action.startsWith('create'))  return 'primary'
  if (action.startsWith('update'))  return 'info'
  return ''
}

const auditDetailHasBody = (detail) => {
  if (detail == null) return false
  return String(detail).trim().length > 0
}

const auditDetailDisplay = (detail) => (auditDetailHasBody(detail) ? String(detail) : '—')

// KPI 摘要 computed
const totalGenerations = computed(() => {
  const pd = insightsData.value?.pagesDistribution || []
  return pd.reduce((s, i) => s + (i.value || 0), 0)
})

const topModelName = computed(() => {
  const mu = insightsData.value?.modelUsage || []
  if (!mu.length) return '—'
  const name = mu[0].model
  // 截短显示
  if (name.length > 12) return name.slice(0, 10) + '…'
  return name
})

const topModelPct = computed(() => {
  const mu = insightsData.value?.modelUsage || []
  if (!mu.length) return 0
  const total = mu.reduce((s, m) => s + m.count, 0)
  return total ? Math.round((mu[0].count / total) * 100) : 0
})

const peakHour = computed(() => {
  const cells = insightsData.value?.hourlyHeatmap || []
  if (!cells.length) return '—'
  const hourMap = {}
  for (const c of cells) {
    hourMap[c.hour] = (hourMap[c.hour] || 0) + c.count
  }
  let maxH = 0, maxV = 0
  for (const [h, v] of Object.entries(hourMap)) {
    if (v > maxV) { maxV = v; maxH = Number(h) }
  }
  return `${String(maxH).padStart(2, '0')}:00`
})

const peakCount = computed(() => {
  const cells = insightsData.value?.hourlyHeatmap || []
  if (!cells.length) return 0
  const hourMap = {}
  for (const c of cells) hourMap[c.hour] = (hourMap[c.hour] || 0) + c.count
  return Math.max(...Object.values(hourMap), 0)
})

// 统一字体配置
const CHART_FONT = "'Plus Jakarta Sans', 'PingFang SC', sans-serif"
const CHART_TEXT_COLOR = '#64748b'
const CHART_LINE_COLOR = '#e2e8f0'

// 用户漏斗
const funnelChartOption = computed(() => {
  const d = insightsData.value
  const reg  = d?.userFunnel?.registered     || 0
  const gen1 = d?.userFunnel?.generatedOnce  || 0
  const genM = d?.userFunnel?.generatedMulti || 0
  return {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',
      formatter: '{b}: <b>{c}</b> 人',
      backgroundColor: 'rgba(15,23,42,0.85)',
      borderColor: 'rgba(148,163,184,0.2)',
      textStyle: { color: '#f1f5f9', fontFamily: CHART_FONT, fontSize: 13 }
    },
    series: [{
      type: 'funnel',
      top: 20, bottom: 20,
      left: '8%', width: '84%',
      minSize: '28%',
      sort: 'descending',
      gap: 6,
      label: {
        show: true,
        position: 'inside',
        fontFamily: CHART_FONT,
        fontSize: 13,
        fontWeight: 600,
        color: '#fff',
        formatter: (p) => `${p.name}\n${p.value.toLocaleString()} 人`
      },
      itemStyle: { borderWidth: 0, borderRadius: 4 },
      data: [
        { value: reg,  name: '注册用户',
          itemStyle: { color: { type: 'linear', x: 0, y: 0, x2: 1, y2: 0, colorStops: [{ offset: 0, color: '#38bdf8' }, { offset: 1, color: '#0ea5e9' }] } } },
        { value: gen1, name: '生成过 PPT',
          itemStyle: { color: { type: 'linear', x: 0, y: 0, x2: 1, y2: 0, colorStops: [{ offset: 0, color: '#818cf8' }, { offset: 1, color: '#6366f1' }] } } },
        { value: genM, name: '高频用户 ≥3次',
          itemStyle: { color: { type: 'linear', x: 0, y: 0, x2: 1, y2: 0, colorStops: [{ offset: 0, color: '#34d399' }, { offset: 1, color: '#10b981' }] } } }
      ]
    }]
  }
})

// 模型饼图
const PALETTE_PIE = ['#0ea5e9', '#6366f1', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#06b6d4', '#84cc16']
const modelPieChartOption = computed(() => {
  const items = (insightsData.value?.modelUsage || []).map((m, i) => ({
    name: m.model,
    value: m.count,
    itemStyle: { color: PALETTE_PIE[i % PALETTE_PIE.length] }
  }))
  return {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',
      formatter: '{b}<br/>使用 <b>{c}</b> 次 &nbsp;({d}%)',
      backgroundColor: 'rgba(15,23,42,0.85)',
      borderColor: 'rgba(148,163,184,0.2)',
      textStyle: { color: '#f1f5f9', fontFamily: CHART_FONT, fontSize: 13 }
    },
    legend: {
      orient: 'vertical', right: 8, top: 'middle',
      type: 'scroll',
      icon: 'circle',
      itemWidth: 8, itemHeight: 8, itemGap: 10,
      textStyle: { fontFamily: CHART_FONT, fontSize: 12, color: CHART_TEXT_COLOR }
    },
    series: [{
      type: 'pie',
      radius: ['42%', '68%'],
      center: ['38%', '50%'],
      avoidLabelOverlap: true,
      data: items,
      label: { show: false },
      labelLine: { show: false },
      emphasis: {
        scale: true, scaleSize: 8,
        itemStyle: { shadowBlur: 20, shadowColor: 'rgba(14,165,233,0.4)' }
      },
      animationType: 'scale',
      animationEasing: 'elasticOut'
    }]
  }
})

// 页数分布柱状图
const pagesBarChartOption = computed(() => {
  const items = insightsData.value?.pagesDistribution || []
  const maxVal = Math.max(...items.map((i) => i.value), 1)
  return {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow', shadowStyle: { color: 'rgba(14,165,233,0.06)' } },
      backgroundColor: 'rgba(15,23,42,0.85)',
      borderColor: 'rgba(148,163,184,0.2)',
      textStyle: { color: '#f1f5f9', fontFamily: CHART_FONT, fontSize: 13 }
    },
    grid: { left: 48, right: 20, top: 28, bottom: 36 },
    xAxis: {
      type: 'category',
      data: items.map((i) => i.label),
      axisLine: { lineStyle: { color: CHART_LINE_COLOR } },
      axisTick: { show: false },
      axisLabel: { fontFamily: CHART_FONT, fontSize: 12, color: CHART_TEXT_COLOR }
    },
    yAxis: {
      type: 'value',
      name: '次数',
      nameTextStyle: { fontFamily: CHART_FONT, fontSize: 11, color: CHART_TEXT_COLOR },
      splitLine: { lineStyle: { color: CHART_LINE_COLOR, type: 'dashed' } },
      axisLabel: { fontFamily: CHART_FONT, fontSize: 11, color: CHART_TEXT_COLOR }
    },
    series: [{
      type: 'bar',
      barMaxWidth: 56,
      data: items.map((item) => ({
        value: item.value,
        itemStyle: {
          borderRadius: [6, 6, 0, 0],
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: item.value === maxVal ? '#f59e0b' : '#38bdf8' },
              { offset: 1, color: item.value === maxVal ? '#d97706' : '#0ea5e9' }
            ]
          }
        }
      })),
      label: {
        show: true, position: 'top',
        fontFamily: CHART_FONT, fontSize: 12, fontWeight: 600,
        color: CHART_TEXT_COLOR
      }
    }]
  }
})

// ── 词云渲染 ──────────────────────────────────────────────────────────────────
const WORD_CLOUD_COLORS = [
  '#4f46e5', '#0ea5e9', '#10b981', '#f59e0b', '#ef4444',
  '#8b5cf6', '#06b6d4', '#84cc16', '#f97316', '#ec4899',
  '#6366f1', '#14b8a6', '#a3e635', '#fb923c', '#e879f9'
]

function drawWordCloud(canvas, words) {
  if (!canvas || !words || !words.length) return
  const dpr = window.devicePixelRatio || 1
  const rect = canvas.parentElement.getBoundingClientRect()
  const W = rect.width || 420
  const H = 300
  canvas.width = W * dpr
  canvas.height = H * dpr
  canvas.style.width = W + 'px'
  canvas.style.height = H + 'px'
  const ctx = canvas.getContext('2d')
  ctx.scale(dpr, dpr)
  ctx.clearRect(0, 0, W, H)

  const maxCount = Math.max(...words.map((w) => w.count), 1)
  const minCount = Math.min(...words.map((w) => w.count), 1)
  const countRange = maxCount - minCount || 1

  // Font size range: min 12px, max 46px
  const minFont = 12, maxFont = 46
  const getFontSize = (count) =>
    Math.round(minFont + ((count - minCount) / countRange) * (maxFont - minFont))

  // Spiral placement: try to place words starting from center, spiraling outward
  const placed = []
  const checkOverlap = (x, y, w, h) => {
    const pad = 4
    for (const p of placed) {
      if (
        x - pad < p.x + p.w &&
        x + w + pad > p.x &&
        y - pad < p.y + p.h &&
        y + h + pad > p.y
      ) return true
    }
    return false
  }

  const cx = W / 2, cy = H / 2
  // Sort by count desc so hottest words go to center
  const sorted = [...words].sort((a, b) => b.count - a.count).slice(0, 40)

  sorted.forEach((word, idx) => {
    const fontSize = getFontSize(word.count)
    ctx.font = `${word.count === maxCount ? 700 : 600} ${fontSize}px "PingFang SC", "Microsoft YaHei", sans-serif`
    const tw = ctx.measureText(word.keyword).width
    const th = fontSize * 1.2

    let placed_x = null, placed_y = null
    // Archimedean spiral search
    for (let angle = 0, r = 0; r < Math.max(W, H); angle += 0.3, r = 0.08 * angle) {
      const px = cx + r * Math.cos(angle) - tw / 2
      const py = cy + r * Math.sin(angle) + th / 2
      if (px < 4 || py < 4 || px + tw > W - 4 || py > H - 4) continue
      if (!checkOverlap(px, py - th, tw, th)) {
        placed_x = px
        placed_y = py
        break
      }
    }

    if (placed_x === null) return

    placed.push({ x: placed_x, y: placed_y - th, w: tw, h: th })
    const color = WORD_CLOUD_COLORS[idx % WORD_CLOUD_COLORS.length]

    // Subtle glow for hottest words
    if (word.count >= maxCount * 0.7) {
      ctx.shadowColor = color
      ctx.shadowBlur = 8
    } else {
      ctx.shadowBlur = 0
    }

    ctx.fillStyle = color
    ctx.globalAlpha = 0.85 + 0.15 * ((word.count - minCount) / countRange)
    ctx.fillText(word.keyword, placed_x, placed_y)
    ctx.shadowBlur = 0
    ctx.globalAlpha = 1
  })
}

watch(
  () => [insightsData.value?.topTopics, wordCloudCanvas.value],
  async () => {
    await nextTick()
    if (wordCloudCanvas.value && insightsData.value?.topTopics?.length) {
      drawWordCloud(wordCloudCanvas.value, insightsData.value.topTopics)
    }
  },
  { deep: true, immediate: true }
)

// 热力图 (24h × 7day)
const WEEKDAY_LABELS = ['周一', '周二', '周三', '周四', '周五', '周六', '周日']
const HOUR_LABELS = Array.from({ length: 24 }, (_, i) => `${String(i).padStart(2, '0')}:00`)

const heatmapChartOption = computed(() => {
  const cells = insightsData.value?.hourlyHeatmap || []
  const maxVal = cells.reduce((m, c) => Math.max(m, c.count), 1)
  const data = cells.map((c) => [c.hour, c.weekday, c.count])
  return {
    backgroundColor: 'transparent',
    tooltip: {
      formatter: (p) => {
        const d = p.data
        return `<b>${WEEKDAY_LABELS[d[1]]}</b> ${HOUR_LABELS[d[0]]}<br/>生成 <b>${d[2]}</b> 次`
      },
      backgroundColor: 'rgba(15,23,42,0.85)',
      borderColor: 'rgba(148,163,184,0.2)',
      textStyle: { color: '#f1f5f9', fontFamily: CHART_FONT, fontSize: 13 }
    },
    grid: { left: 52, right: 20, top: 12, bottom: 48 },
    xAxis: {
      type: 'category',
      data: HOUR_LABELS,
      splitArea: { show: true, areaStyle: { color: ['rgba(248,250,252,0.6)', 'rgba(241,245,249,0.4)'] } },
      axisLine: { lineStyle: { color: CHART_LINE_COLOR } },
      axisTick: { show: false },
      axisLabel: { fontFamily: CHART_FONT, fontSize: 10, color: CHART_TEXT_COLOR, interval: 1, rotate: 40 }
    },
    yAxis: {
      type: 'category',
      data: WEEKDAY_LABELS,
      splitArea: { show: true, areaStyle: { color: ['rgba(248,250,252,0.6)', 'rgba(241,245,249,0.4)'] } },
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: { fontFamily: CHART_FONT, fontSize: 12, color: '#334155' }
    },
    visualMap: {
      min: 0, max: maxVal,
      calculable: true,
      orient: 'horizontal',
      left: 'center', bottom: 2,
      itemHeight: 120,
      textStyle: { fontFamily: CHART_FONT, fontSize: 11, color: CHART_TEXT_COLOR },
      inRange: { color: ['#f0f9ff', '#7dd3fc', '#0369a1'] }
    },
    series: [{
      type: 'heatmap',
      data,
      itemStyle: { borderRadius: 3, borderWidth: 2, borderColor: '#fff' },
      emphasis: { itemStyle: { shadowBlur: 12, shadowColor: 'rgba(14,165,233,0.5)' } },
      progressive: 500
    }]
  }
})

const deleteMaterial = async (row) => {
  try {
    const { value: reason } = await ElMessageBox.prompt(
      `确定删除素材「${row.filename}」？请填写删除原因（将通知用户）。`,
      '删除确认',
      {
        confirmButtonText: '确认删除',
        cancelButtonText: '取消',
        inputPlaceholder: '请输入删除原因，如：违规内容、版权问题等',
        inputType: 'textarea',
        inputValidator: (v) => v && v.trim().length > 0 ? true : '请填写删除原因',
        type: 'warning'
      }
    )
    await adminAPI.deleteMaterial(row.id, reason.trim())
    ElMessage.success('删除成功，已通知用户')
    await loadMaterials()
    await loadMaterialStats()
  } catch (e) {
    if (e !== 'cancel' && e !== 'close') {
      console.error('删除素材失败:', e?.userMessage || e)
    }
  }
}

// ── 素材详情抽屉方法 ────────────────────────────────────────────────────────
const formatExtractContent = (extractResult) => {
  if (!extractResult) return ''
  if (typeof extractResult === 'string') return extractResult
  // JSON 对象：优先取 text 或 content 字段
  if (extractResult.text) return extractResult.text
  if (extractResult.content) return extractResult.content
  if (Array.isArray(extractResult.sections)) {
    return extractResult.sections.map((s) => s.content || '').join('\n\n')
  }
  return JSON.stringify(extractResult, null, 2)
}

const openMaterialDrawer = async (row, tab = 'preview') => {
  materialDrawer.visible = true
  materialDrawer.data = null
  materialDrawer.reviewing = false
  materialDrawer.reviewResult = null
  materialDrawer.activeTab = tab
  materialDrawer.fileUrl = ''
  materialDrawer.previewError = false

  try {
    const res = await adminAPI.getMaterialContent(row.id)
    materialDrawer.data = res.data || {}
    // 构造文件预览 URL（带 token）
    materialDrawer.fileUrl = adminAPI.getMaterialFileUrl(row.id)
    // 如果数据库中已有审核结论，直接展示
    if (materialDrawer.data.reviewResult) {
      materialDrawer.reviewResult = materialDrawer.data.reviewResult
    }
  } catch (e) {
    ElMessage.error('获取素材详情失败')
    materialDrawer.visible = false
  }
}

const openFileInNewTab = () => {
  if (materialDrawer.fileUrl) {
    window.open(materialDrawer.fileUrl, '_blank', 'noopener,noreferrer')
  }
}

const triggerReview = async () => {
  if (!materialDrawer.data?.id) return
  materialDrawer.reviewing = true
  try {
    const res = await adminAPI.reviewMaterial(materialDrawer.data.id)
    materialDrawer.reviewResult = res.data
    // 同步更新列表中的行（如果能找到）
    const row = materials.value.find((m) => m.id === materialDrawer.data.id)
    if (row) {
      row.reviewResult = res.data
    }
    ElMessage.success('AI 审核完成')
  } catch (e) {
    ElMessage.error(e?.response?.data?.message || 'AI 审核失败，请稍后重试')
  } finally {
    materialDrawer.reviewing = false
  }
}

const deleteFromDrawer = async () => {
  if (!materialDrawer.data?.id) return
  // 违规删除：预填原因
  const defaultReason = materialDrawer.reviewResult?.reason
    ? `AI 审核违规：${materialDrawer.reviewResult.reason}`
    : 'AI 审核判定存在违规内容'
  try {
    const { value: reason } = await ElMessageBox.prompt(
      `确认删除违规素材「${materialDrawer.data.filename}」？请确认或修改删除原因。`,
      '确认删除违规素材',
      {
        confirmButtonText: '确认删除',
        cancelButtonText: '取消',
        inputPlaceholder: '删除原因',
        inputType: 'textarea',
        inputValue: defaultReason,
        inputValidator: (v) => v && v.trim().length > 0 ? true : '请填写删除原因',
        type: 'error'
      }
    )
    await adminAPI.deleteMaterial(materialDrawer.data.id, reason.trim())
    ElMessage.success('违规素材已删除，已通知用户')
    materialDrawer.visible = false
    await loadMaterials()
    await loadMaterialStats()
  } catch (e) {
    if (e !== 'cancel' && e !== 'close') {
      ElMessage.error('删除失败')
    }
  }
}

const batchDeleteMaterials = async () => {
  if (materialSelection.value.length === 0) return
  const ids = materialSelection.value.map((r) => r.id)
  try {
    const { value: reason } = await ElMessageBox.prompt(
      `确定删除选中的 ${ids.length} 个素材？请填写删除原因（将统一通知各用户）。`,
      '批量删除确认',
      {
        confirmButtonText: '确认删除',
        cancelButtonText: '取消',
        inputPlaceholder: '请输入删除原因',
        inputType: 'textarea',
        inputValidator: (v) => v && v.trim().length > 0 ? true : '请填写删除原因',
        type: 'warning'
      }
    )
    const res = await adminAPI.batchDeleteMaterials(ids, reason.trim())
    const { success, failed } = res.data || {}
    ElMessage.success(`删除完成：成功 ${success}，失败 ${failed}，已通知用户`)
    materialSelection.value = []
    await loadMaterials()
    await loadMaterialStats()
  } catch (e) {
    if (e !== 'cancel' && e !== 'close') {
      console.error('批量删除失败:', e?.userMessage || e)
    }
  }
}

watch(activeNav, (value) => {
  if (value === 'users' && users.value.length === 0 && !usersLoading.value) {
    loadUsers()
  }
  if (value === 'materials' && materials.value.length === 0 && !materialsLoading.value) {
    loadMaterials(1)
    loadMaterialStats()
  }
  if (value === 'announcements' && announcements.value.length === 0 && !announcementsLoading.value) {
    loadAnnouncements(1)
  }
  if (value === 'insights') {
    if (!insightsData.value && !insightsLoading.value) {
      loadInsights()
    }
    // 数据看板图表也在此 tab 内，确保已加载
    if (!metricsLoading.value) {
      loadMetrics()
    }
  }
  if (value === 'audit' && auditLogs.value.length === 0 && !auditLoading.value) {
    loadAuditLogs(1)
  }
  if (value === 'settings' && settingsItems.value.length === 0 && !settingsLoading.value) {
    loadSettings()
  }
})

// ── 模板管理 ────────────────────────────────────────────────────────────────
const tmplMgrLoading = ref(false)
const tmplMgrList = ref([])
const tmplMgrSearch = ref('')
/** 记录加载失败的缩略图 id，避免无限重试（reactive Set 使 Vue 能追踪 add 操作） */
const tmplPreviewFailed = reactive(new Set())

/**
 * 将后端返回的 previewImage 解析为可用于 <img src> 的完整 URL。
 * - 若后端已返回完整 URL（http/https），直接使用。
 * - 若为相对路径（/api/templates/preview?id=xxx），需要拼上 API base，
 *   避免在 ngrok/反代场景下路径丢失。
 * - 若 previewImage 为空，构造 /api/templates/preview?id=xxx 路径。
 */
function resolveTmplPreviewUrl(tmpl) {
  const raw = tmpl.previewImage || ''
  const base = (import.meta.env.VITE_API_URL || '/api').replace(/\/+$/, '')
  if (raw.startsWith('http://') || raw.startsWith('https://')) {
    return raw
  }
  if (raw.startsWith('/api/')) {
    // 相对 /api 路径，拼上实际 base（处理 base 不是 /api 的情况）
    const suffix = raw.slice(4) // 去掉 /api
    return base + suffix
  }
  if (raw) {
    return raw
  }
  // 完全没有预览图，回退到后端预览接口
  return `${base}/templates/preview?id=${encodeURIComponent(tmpl.id)}`
}

const tmplMgrFiltered = computed(() => {
  const q = tmplMgrSearch.value.trim().toLowerCase()
  if (!q) return tmplMgrList.value
  return tmplMgrList.value.filter(
    (t) => t.name.toLowerCase().includes(q) || (t.provider || '').toLowerCase().includes(q)
  )
})

async function loadTemplateList() {
  tmplMgrLoading.value = true
  tmplPreviewFailed.clear()
  try {
    const res = await adminAPI.templateList()
    tmplMgrList.value = res.data?.items || []
  } catch (e) {
    ElMessage.error('模板列表加载失败：' + (e?.response?.data?.message || e.message))
  } finally {
    tmplMgrLoading.value = false
  }
}

function formatTmplDate(dateStr) {
  if (!dateStr) return '-'
  return dayjs(dateStr).format('YYYY/MM/DD HH:mm')
}

// 上架对话框
const activateDialog = reactive({
  visible: false,
  loading: false,
  tmpl: null,
  from: '',
  to: ''
})

function openActivateDialog(tmpl) {
  activateDialog.tmpl = tmpl
  activateDialog.from = tmpl.listing?.availableFrom
    ? dayjs(tmpl.listing.availableFrom).format('YYYY-MM-DD HH:mm:ss')
    : ''
  activateDialog.to = tmpl.listing?.availableTo
    ? dayjs(tmpl.listing.availableTo).format('YYYY-MM-DD HH:mm:ss')
    : ''
  activateDialog.visible = true
}

async function confirmActivate() {
  if (!activateDialog.tmpl) return
  activateDialog.loading = true
  try {
    const payload = { templateId: activateDialog.tmpl.id }
    if (activateDialog.from) payload.availableFrom = activateDialog.from
    if (activateDialog.to) payload.availableTo = activateDialog.to
    await adminAPI.activateTemplate(payload)
    ElMessage.success(`模板「${activateDialog.tmpl.name}」已上架`)
    activateDialog.visible = false
    await loadTemplateList()
  } catch (e) {
    ElMessage.error('上架失败：' + (e?.response?.data?.message || e.message))
  } finally {
    activateDialog.loading = false
  }
}

async function handleDeactivate(tmpl) {
  try {
    await ElMessageBox.confirm(
      `确认下架模板「${tmpl.name}」？下架后用户端将不再显示该模板。`,
      '下架确认',
      { type: 'warning', confirmButtonText: '确认下架', cancelButtonText: '取消' }
    )
  } catch {
    return
  }
  try {
    await adminAPI.deactivateTemplate({ templateId: tmpl.id })
    ElMessage.success(`模板「${tmpl.name}」已下架`)
    await loadTemplateList()
  } catch (e) {
    ElMessage.error('下架失败：' + (e?.response?.data?.message || e.message))
  }
}

async function handleRemoveRecord(tmpl) {
  try {
    await ElMessageBox.confirm(
      `确认清除模板「${tmpl.name}」的上架记录？此操作不删除模板文件本身，仅清除数据库中的上架信息。`,
      '清除记录确认',
      { type: 'warning', confirmButtonText: '确认清除', cancelButtonText: '取消' }
    )
  } catch {
    return
  }
  try {
    await adminAPI.removeTemplateRecord(tmpl.id)
    ElMessage.success('上架记录已清除')
    await loadTemplateList()
  } catch (e) {
    ElMessage.error('清除失败：' + (e?.response?.data?.message || e.message))
  }
}

// ── OfficePLUS 导入 ──────────────────────────────────────────────────────────
const tmplActiveTab = ref('local')   // 'local' | 'officeplus'
const opImportLogs = ref([])
const opPendingFiles = ref([])       // File[]
const opDragover = ref(false)
const opBatchUploading = ref(false)
const opBatchProgress = ref({ done: 0, total: 0 })

function openOpSite() {
  window.open('https://www.officeplus.cn/PPT/template/', '_blank')
}

function onOpDrop(e) {
  opDragover.value = false
  const files = [...(e.dataTransfer?.files || [])].filter(f =>
    f.name.toLowerCase().endsWith('.pptx')
  )
  if (!files.length) {
    ElMessage.warning('请拖入 .pptx 格式的文件')
    return
  }
  opPendingFiles.value.push(...files)
}

function onOpFileSelect(e) {
  const files = [...(e.target.files || [])].filter(f =>
    f.name.toLowerCase().endsWith('.pptx')
  )
  if (files.length) opPendingFiles.value.push(...files)
  // reset input so same file can be re-selected
  e.target.value = ''
}

/** File → base64 string */
function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => {
      // result is "data:...;base64,<actual>" — strip prefix
      const b64 = reader.result.split(',')[1]
      resolve(b64)
    }
    reader.onerror = reject
    reader.readAsDataURL(file)
  })
}

async function doOpBatchUpload() {
  if (!opPendingFiles.value.length) return
  opBatchUploading.value = true
  opBatchProgress.value = { done: 0, total: opPendingFiles.value.length }

  // 逐个发送，每次一个文件，避免单次 body 过大（base64 膨胀约 1.35x）
  const BATCH_SIZE = 1
  const files = [...opPendingFiles.value]
  const allResults = []

  for (let i = 0; i < files.length; i += BATCH_SIZE) {
    const chunk = files.slice(i, i + BATCH_SIZE)
    const fileItems = await Promise.all(chunk.map(async f => ({
      filename: f.name,
      file_base64: await fileToBase64(f),
    })))

    try {
      const res = await adminAPI.officeplusBatchUpload({ files: fileItems })
      const data = res.data || {}
      for (const r of (data.results || [])) {
        allResults.push(r)
        opImportLogs.value.unshift({
          ok: r.ok,
          text: r.ok
            ? `「${r.filename}」上传成功 → ${r.templateId}`
            : `「${r.filename}」失败：${r.error || '未知错误'}`,
          size: r.ok && r.fileSize ? `${(r.fileSize/1024/1024).toFixed(2)} MB` : '',
        })
      }
    } catch (e) {
      for (const f of chunk) {
        opImportLogs.value.unshift({
          ok: false,
          text: `「${f.name}」上传失败：${e?.response?.data?.message || e.message}`,
          size: '',
        })
      }
    }
    opBatchProgress.value.done = Math.min(i + BATCH_SIZE, files.length)
  }

  // 保持日志最多 50 条
  if (opImportLogs.value.length > 50) opImportLogs.value.length = 50

  const okCount = allResults.filter(r => r.ok).length
  const failCount = allResults.length - okCount
  if (okCount > 0) {
    await loadTemplateList()
    ElMessage.success(`${okCount} 个模板上传成功${failCount ? `，${failCount} 个失败` : ''}，可在「本地模板库」中上架`)
    // 自动切换到本地模板库，让管理员立即看到新模板
    tmplActiveTab.value = 'local'
  } else if (failCount > 0) {
    ElMessage.error(`${failCount} 个文件上传失败`)
  }

  opPendingFiles.value = []
  opBatchUploading.value = false
  opBatchProgress.value = { done: 0, total: 0 }
}

// 切换到模板管理 tab 时自动加载
watch(
  () => activeNav.value,
  (val) => {
    if (val === 'templates' && tmplMgrList.value.length === 0) {
      loadTemplateList()
    }
  }
)

onMounted(() => {
  if (currentUser.value?.isAdmin) {
    store.dispatch('fetchAdminHistory').catch(() => {})
    loadMetrics()
    loadUsers()
  }
})
</script>

<style scoped>
@import url('https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600;700&family=Fira+Code:wght@400;500;600;700&display=swap');

.admin-shell {
  min-height: 100vh;
  display: grid;
  grid-template-columns: auto 1fr;
  background: radial-gradient(circle at top, rgba(14, 165, 233, 0.12), transparent 50%),
    linear-gradient(135deg, #f8fafc 0%, #f0f9ff 45%, #e0f2fe 100%);
  font-family: 'Plus Jakarta Sans', 'Noto Sans SC', 'PingFang SC', sans-serif;
  color: #0f172a;
}

.admin-sidebar {
  width: 280px;
  padding: var(--space-xl) var(--space-lg);
  background: linear-gradient(180deg, rgba(15, 23, 42, 0.95), rgba(15, 23, 42, 0.9));
  color: #e2e8f0;
  display: flex;
  flex-direction: column;
  gap: var(--space-xl);
  position: relative;
  overflow: visible;
  transition: width 0.25s ease;
}

.admin-sidebar::after {
  content: '';
  position: absolute;
  inset: 0;
  background-image: radial-gradient(rgba(148, 163, 184, 0.15) 1px, transparent 0);
  background-size: 18px 18px;
  opacity: 0.5;
  pointer-events: none;
}

.brand-block {
  position: relative;
  z-index: 1;
}

.brand-logo {
  font-size: 1.6rem;
  font-weight: 700;
  letter-spacing: 0.08em;
}

.brand-subtitle {
  font-size: 0.9rem;
  color: rgba(226, 232, 240, 0.75);
  margin-top: 6px;
}

.admin-user {
  display: flex;
  align-items: center;
  gap: var(--space-md);
  padding: var(--space-md);
  background: rgba(148, 163, 184, 0.12);
  border-radius: var(--radius-card);
  position: relative;
  z-index: 1;
}

.admin-avatar {
  width: 46px;
  height: 46px;
  border-radius: var(--radius-card);
  background: linear-gradient(135deg, #38bdf8, #0EA5E9);
  display: grid;
  place-items: center;
  font-weight: 700;
  color: #0f172a;
}

.admin-user-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.admin-name {
  font-weight: 600;
  font-size: 1rem;
}

.admin-role {
  font-size: 0.8rem;
  color: rgba(226, 232, 240, 0.7);
}

.admin-nav {
  display: flex;
  flex-direction: column;
  gap: 10px;
  position: relative;
  z-index: 1;
}

.nav-item {
  border: none;
  background: rgba(148, 163, 184, 0.08);
  color: #e2e8f0;
  padding: var(--space-md);
  border-radius: var(--radius-card);
  display: flex;
  align-items: center;
  gap: var(--space-sm);
  cursor: pointer;
  transition: background var(--transition-default), color var(--transition-default), transform var(--transition-default);
  text-align: left;
  position: relative;
}

.nav-item:hover:not(.disabled),
.nav-item.active {
  background: rgba(14, 165, 233, 0.2);
  color: #ffffff;
  transform: translateX(4px);
}

.nav-item.disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

.nav-icon {
  width: 34px;
  height: 34px;
  border-radius: 12px;
  background: rgba(15, 23, 42, 0.6);
  display: grid;
  place-items: center;
}

.nav-label {
  flex: 1;
  font-weight: 500;
}

.nav-badge {
  background: #f97316;
  color: #0f172a;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.7rem;
  font-weight: 700;
}

.nav-footer {
  margin-top: auto;
  position: relative;
  z-index: 1;
}

.back-btn {
  width: 100%;
  padding: var(--space-sm) var(--space-md);
  border-radius: var(--radius-card);
  border: 1px solid rgba(148, 163, 184, 0.3);
  background: transparent;
  color: #e2e8f0;
  display: flex;
  align-items: center;
  gap: var(--space-xs);
  cursor: pointer;
  transition: background var(--transition-default), color var(--transition-default);
}

.back-btn:hover {
  background: rgba(148, 163, 184, 0.2);
}

/* 收起按钮：固定在侧栏右侧中部 */
.sidebar-toggle {
  position: absolute;
  right: -14px;
  top: 50%;
  transform: translateY(-50%);
  z-index: 20;
  width: 28px;
  height: 28px;
  padding: 0;
  border-radius: 50%;
  border: 1px solid rgba(226, 232, 240, 0.25);
  background: linear-gradient(180deg, rgba(15, 23, 42, 0.98), rgba(15, 23, 42, 0.95));
  color: #e2e8f0;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
  transition: background var(--transition-default), color var(--transition-default), transform var(--transition-default);
}

.sidebar-toggle:hover {
  background: rgba(14, 165, 233, 0.4);
  color: #fff;
  transform: translateY(-50%) scale(1.05);
}

/* 收起态：适当收窄（约 96px），顶部品牌+用户信息缩小字号以便基本完整显示 */
.admin-sidebar--collapsed {
  width: 96px;
  padding: 12px 8px;
}

.admin-sidebar--collapsed .brand-block {
  text-align: center;
  width: 100%;
  flex-shrink: 0;
}

.admin-sidebar--collapsed .brand-logo {
  font-size: 0.75rem;
  font-weight: 700;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 100%;
  display: block;
  line-height: 1.2;
}

.admin-sidebar--collapsed .brand-subtitle {
  font-size: 0.6rem;
  margin-top: 2px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 100%;
  color: rgba(226, 232, 240, 0.7);
  line-height: 1.2;
}

.admin-sidebar--collapsed .admin-user {
  flex-direction: column;
  align-items: center;
  padding: 6px 0;
  gap: 4px;
  width: 100%;
  flex-shrink: 0;
}

.admin-sidebar--collapsed .admin-avatar {
  width: 32px;
  height: 32px;
  font-size: 0.8rem;
  flex-shrink: 0;
}

.admin-sidebar--collapsed .admin-user-info {
  overflow: hidden;
  width: 100%;
  text-align: center;
  min-width: 0;
}

.admin-sidebar--collapsed .admin-name {
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 0.65rem;
  line-height: 1.25;
}

.admin-sidebar--collapsed .admin-role {
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 0.58rem;
  margin-top: 1px;
  color: rgba(226, 232, 240, 0.75);
}

.admin-sidebar--collapsed .nav-label,
.admin-sidebar--collapsed .nav-badge,
.admin-sidebar--collapsed .back-btn-text {
  position: absolute;
  opacity: 0;
  overflow: hidden;
  white-space: nowrap;
  width: 0;
  height: 0;
  padding: 0;
  margin: 0;
  visibility: hidden;
  pointer-events: none;
}

/* 收起态：导航项统一尺寸，图标居中对齐 */
.admin-sidebar--collapsed .admin-nav {
  gap: 6px;
  width: 100%;
}

.admin-sidebar--collapsed .nav-item {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: 0;
  padding: 0;
  min-height: 44px;
  width: 100%;
  transform: none;
}

.admin-sidebar--collapsed .nav-item:hover:not(.disabled),
.admin-sidebar--collapsed .nav-item.active {
  transform: none;
}

.admin-sidebar--collapsed .nav-icon {
  width: 32px;
  height: 32px;
  margin: 0;
  flex-shrink: 0;
  display: grid;
  place-items: center;
}

.admin-sidebar--collapsed .nav-icon .el-icon {
  font-size: 1.15rem;
}

.admin-sidebar--collapsed .nav-footer {
  padding: 0;
  margin-top: auto;
}

.admin-sidebar--collapsed .back-btn {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: 0;
  padding: 0;
  min-height: 44px;
  width: 100%;
}

.admin-sidebar--collapsed .back-btn .el-icon {
  font-size: 1.15rem;
  margin: 0;
}

.admin-main {
  padding: var(--space-xl) var(--space-2xl);
  display: flex;
  flex-direction: column;
  gap: var(--space-xl);
}

.admin-topbar {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: var(--space-lg);
}

.admin-topbar h1 {
  font-size: var(--text-title-page);
  font-weight: 600;
  margin-bottom: var(--space-xs);
  line-height: var(--line-height-tight);
}

.admin-topbar p {
  color: var(--color-text-muted);
  font-size: var(--text-caption);
}

.top-actions {
  display: flex;
  gap: var(--space-sm);
  flex-wrap: wrap;
}

.ghost-btn {
  background: rgba(255, 255, 255, 0.7);
  border: 1px solid rgba(148, 163, 184, 0.4);
  color: #0f172a;
}

.admin-content {
  display: flex;
  flex-direction: column;
  gap: 28px;
}

.overview-section,
.records-section {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.overview-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.overview-header p {
  color: #64748b;
}

.overview-meta {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.meta-chip {
  padding: 6px 12px;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.08);
  font-size: 0.85rem;
}

.meta-chip.accent {
  background: rgba(14, 165, 233, 0.15);
  color: #0284C7;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: var(--space-md);
}

.stat-card {
  padding: var(--space-lg);
  border-radius: var(--radius-card);
  background: var(--color-bg-card);
  border: 1px solid rgba(0, 0, 0, 0.06);
  display: flex;
  gap: var(--space-md);
  align-items: center;
  box-shadow: var(--shadow-card);
  transition: box-shadow var(--transition-default), transform var(--transition-default);
}

.stat-card:hover {
  box-shadow: var(--shadow-card-hover);
  transform: translateY(-2px);
}

.stat-icon {
  width: 48px;
  height: 48px;
  border-radius: var(--radius-card);
  background: linear-gradient(135deg, #38bdf8, #0EA5E9);
  display: grid;
  place-items: center;
  color: #0f172a;
}

.stat-info h3 {
  font-size: var(--text-kpi);
  font-weight: 700;
  margin: var(--space-xs) 0;
  line-height: var(--line-height-tight);
}

.stat-info p {
  color: var(--color-text-muted);
  font-size: var(--text-caption);
}

.stat-footnote {
  font-size: 0.75rem;
  color: #94a3b8;
}

.overview-insights {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 16px;
}

/* ══════════════════════════════════════════════════════════════════════════
   偏好洞察 — 全新设计系统
   ══════════════════════════════════════════════════════════════════════════ */

/* 外层容器：撑满主内容区，不受 records-section padding 约束 */
.ins-shell {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 0;
  /* 继承父级滚动 */
}

/* ── Hero Header ── */
.ins-hero {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 16px;
  padding: 28px 32px 24px;
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 60%, #0c2340 100%);
  border-radius: 16px;
  position: relative;
  overflow: hidden;
}
.ins-hero::before {
  content: '';
  position: absolute;
  inset: 0;
  background-image: radial-gradient(rgba(148, 163, 184, 0.08) 1px, transparent 0);
  background-size: 20px 20px;
  pointer-events: none;
}
.ins-hero::after {
  content: '';
  position: absolute;
  top: -60px; right: -60px;
  width: 220px; height: 220px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(14, 165, 233, 0.18) 0%, transparent 70%);
  pointer-events: none;
}
.ins-hero__text { position: relative; z-index: 1; }
.ins-hero__eyebrow {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.2em;
  color: #38bdf8;
  text-transform: uppercase;
  margin-bottom: 6px;
}
.ins-hero__title {
  font-size: 26px;
  font-weight: 700;
  color: #f8fafc;
  line-height: 1.2;
  margin: 0 0 6px;
}
.ins-hero__sub {
  font-size: 13px;
  color: rgba(226, 232, 240, 0.6);
  margin: 0;
}

/* ── 刷新按钮 ── */
.ins-refresh-btn {
  position: relative; z-index: 1;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 10px 20px;
  background: rgba(14, 165, 233, 0.15);
  border: 1px solid rgba(56, 189, 248, 0.35);
  border-radius: 10px;
  color: #7dd3fc;
  font-size: 13px;
  font-weight: 600;
  font-family: inherit;
  cursor: pointer;
  transition: all 0.2s ease;
  white-space: nowrap;
}
.ins-refresh-btn:hover {
  background: rgba(14, 165, 233, 0.28);
  border-color: rgba(56, 189, 248, 0.6);
  color: #e0f2fe;
  transform: translateY(-1px);
}
.ins-refresh-btn .el-icon {
  font-size: 15px;
  transition: transform 0.6s ease;
}
.ins-refresh-btn.spinning .el-icon {
  animation: ins-spin 1s linear infinite;
}
@keyframes ins-spin {
  to { transform: rotate(360deg); }
}

/* ── 骨架屏 ── */
.ins-skeleton-wrap {
  display: flex;
  flex-direction: column;
  gap: 16px;
  animation: ins-pulse 1.6s ease-in-out infinite;
}
@keyframes ins-pulse {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0.5; }
}
.ins-skeleton-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 14px;
}
.ins-skeleton-row--charts {
  grid-template-columns: 1fr 1fr;
}
.ins-skeleton-kpi {
  height: 108px;
  border-radius: 14px;
  background: linear-gradient(135deg, #f1f5f9, #e2e8f0);
}
.ins-skeleton-chart--half {
  height: 300px;
  border-radius: 14px;
  background: linear-gradient(135deg, #f1f5f9, #e2e8f0);
}
.ins-skeleton-chart--full {
  height: 240px;
  border-radius: 14px;
  background: linear-gradient(135deg, #f1f5f9, #e2e8f0);
  grid-column: span 2;
}

/* ── KPI 摘要条 ── */
.ins-kpi-strip {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 14px;
}
.ins-kpi-card {
  border-radius: 14px;
  padding: 18px 20px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  position: relative;
  overflow: hidden;
  transition: transform 0.2s ease, box-shadow 0.2s ease;
}
.ins-kpi-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 28px rgba(0, 0, 0, 0.1);
}
.ins-kpi-card--blue  { background: linear-gradient(135deg, #e0f2fe, #f0f9ff); border: 1px solid #bae6fd; }
.ins-kpi-card--violet{ background: linear-gradient(135deg, #ede9fe, #f5f3ff); border: 1px solid #ddd6fe; }
.ins-kpi-card--teal  { background: linear-gradient(135deg, #d1fae5, #f0fdf4); border: 1px solid #a7f3d0; }
.ins-kpi-card--amber { background: linear-gradient(135deg, #fef3c7, #fffbeb); border: 1px solid #fde68a; }

.ins-kpi-icon {
  width: 36px; height: 36px;
  border-radius: 10px;
  display: flex; align-items: center; justify-content: center;
}
.ins-kpi-card--blue   .ins-kpi-icon { background: rgba(14,165,233,0.12); color: #0284c7; }
.ins-kpi-card--violet .ins-kpi-icon { background: rgba(99,102,241,0.12); color: #4f46e5; }
.ins-kpi-card--teal   .ins-kpi-icon { background: rgba(16,185,129,0.12); color: #059669; }
.ins-kpi-card--amber  .ins-kpi-icon { background: rgba(245,158,11,0.12); color: #d97706; }
.ins-kpi-icon svg { width: 18px; height: 18px; }

.ins-kpi-body { display: flex; flex-direction: column; gap: 2px; }
.ins-kpi-value {
  font-size: 22px;
  font-weight: 700;
  line-height: 1.15;
  color: #0f172a;
  letter-spacing: -0.02em;
}
.ins-kpi-label {
  font-size: 12px;
  color: #64748b;
  font-weight: 500;
}
.ins-kpi-trend {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding-top: 8px;
  border-top: 1px solid rgba(0,0,0,0.06);
}
.ins-kpi-sub  { font-size: 11px; color: #94a3b8; }
.ins-kpi-pct  { font-size: 12px; font-weight: 700; color: #475569; }

/* ── 图表网格 ── */
.ins-charts-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}
.ins-chart-card {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 14px;
  padding: 18px 20px 14px;
  transition: box-shadow 0.2s ease, border-color 0.2s ease;
}
.ins-chart-card:hover {
  box-shadow: 0 4px 20px rgba(14, 165, 233, 0.1);
  border-color: #bae6fd;
}
.ins-chart-card--half { grid-column: span 1; }
.ins-chart-card--full { grid-column: span 2; }

.ins-chart-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 14px;
}
.ins-chart-dot {
  width: 8px; height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}
.ins-chart-dot--blue   { background: #0ea5e9; box-shadow: 0 0 6px rgba(14,165,233,0.5); }
.ins-chart-dot--violet { background: #6366f1; box-shadow: 0 0 6px rgba(99,102,241,0.5); }
.ins-chart-dot--teal   { background: #10b981; box-shadow: 0 0 6px rgba(16,185,129,0.5); }
.ins-chart-dot--amber  { background: #f59e0b; box-shadow: 0 0 6px rgba(245,158,11,0.5); }
.ins-chart-dot--rose   { background: #f43f5e; box-shadow: 0 0 6px rgba(244,63,94,0.5); }

.ins-chart-title {
  font-size: 13px;
  font-weight: 600;
  color: #1e293b;
  display: flex;
  align-items: center;
  gap: 8px;
}
.ins-chart-badge {
  display: inline-flex;
  align-items: center;
  padding: 2px 8px;
  background: #f1f5f9;
  border-radius: 20px;
  font-size: 11px;
  font-weight: 500;
  color: #64748b;
}

/* ── 词云 ── */
.word-cloud-wrap {
  position: relative;
  width: 100%;
  height: 300px;
  overflow: hidden;
  border-radius: 8px;
  background: linear-gradient(135deg, #f8faff 0%, #f0f4ff 100%);
}
.word-cloud-canvas {
  display: block;
  width: 100%;
  height: 100%;
}
.word-cloud-empty {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #94a3b8;
  font-size: 14px;
}

/* ══ 管理页通用 Shell / Hero ══════════════════════════════════════════════ */
.mgmt-shell {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

/* Hero 变体色 */
.mgmt-hero {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 20px;
  flex-wrap: wrap;
  padding: 26px 30px 20px;
  border-radius: 18px;
  border: 1px solid rgba(255,255,255,0.08);
  box-shadow: 0 6px 28px rgba(15,23,42,0.15), inset 0 1px 0 rgba(255,255,255,0.06);
  position: relative;
  overflow: hidden;
}
.mgmt-hero::before {
  content: '';
  position: absolute;
  top: -50px; right: -50px;
  width: 180px; height: 180px;
  border-radius: 50%;
  pointer-events: none;
  opacity: 0.2;
}
.mgmt-hero--emerald {
  background: linear-gradient(135deg, #064e3b 0%, #065f46 55%, #0d4f3c 100%);
}
.mgmt-hero--emerald::before { background: radial-gradient(circle, #10b981 0%, transparent 70%); }
.mgmt-hero--amber {
  background: linear-gradient(135deg, #451a03 0%, #78350f 55%, #3d2004 100%);
}
.mgmt-hero--amber::before { background: radial-gradient(circle, #f59e0b 0%, transparent 70%); }
.mgmt-hero--violet {
  background: linear-gradient(135deg, #2e1065 0%, #3b0764 55%, #1e1b4b 100%);
}
.mgmt-hero--violet::before { background: radial-gradient(circle, #8b5cf6 0%, transparent 70%); }
.mgmt-hero--slate {
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 60%, #1a2035 100%);
}
.mgmt-hero--slate::before { background: radial-gradient(circle, #475569 0%, transparent 70%); }

.mgmt-hero__eyebrow {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.2em;
  color: rgba(255,255,255,0.45);
  text-transform: uppercase;
  margin-bottom: 6px;
  font-family: 'Fira Code', monospace;
}
.mgmt-hero__title {
  font-size: 24px;
  font-weight: 700;
  color: #f8fafc;
  line-height: 1.15;
  margin: 0 0 5px;
  letter-spacing: -0.01em;
}
.mgmt-hero__sub {
  font-size: 12.5px;
  color: rgba(255,255,255,0.4);
  margin: 0;
  font-family: 'Fira Code', monospace;
}
.mgmt-hero__stats {
  display: flex;
  gap: 8px;
  flex-shrink: 0;
  flex-wrap: wrap;
}
.mgmt-stat-pill {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 10px 18px;
  border-radius: 10px;
  min-width: 66px;
  background: rgba(255,255,255,0.1);
  border: 1px solid rgba(255,255,255,0.15);
}
.mgmt-stat-pill--ok  { background: rgba(16,185,129,0.18); border-color: rgba(16,185,129,0.35); }
.mgmt-stat-pill--err { background: rgba(239,68,68,0.18);  border-color: rgba(239,68,68,0.35); }
.mgmt-stat-pill--warn{ background: rgba(245,158,11,0.18); border-color: rgba(245,158,11,0.35); }
.mgmt-stat-num {
  font-size: 20px;
  font-weight: 700;
  color: #f1f5f9;
  line-height: 1;
  font-family: 'Fira Code', monospace;
}
.mgmt-stat-label { font-size: 11px; color: rgba(255,255,255,0.5); }

/* Toolbar */
.mgmt-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  padding: 12px 16px;
  background: #ffffff;
  border: 1px solid #e8edf4;
  border-radius: 14px;
  box-shadow: 0 1px 6px rgba(15,23,42,0.05);
}

/* Icon button */
.mgmt-icon-btn {
  width: 36px;
  height: 36px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 9px;
  border: 1.5px solid #e2e8f0;
  background: #f8fafc;
  color: #475569;
  cursor: pointer;
  transition: all 0.17s;
  flex-shrink: 0;
}
.mgmt-icon-btn:hover { border-color: #6366f1; color: #6366f1; background: rgba(99,102,241,0.06); }
.mgmt-icon-btn.spinning svg { animation: spin 1s linear infinite; }
@keyframes spin { to { transform: rotate(360deg); } }

.mgmt-danger-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 16px;
  background: #fff;
  color: #dc2626;
  border: 1.5px solid #fecaca;
  border-radius: 9px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.17s;
  white-space: nowrap;
}
.mgmt-danger-btn:hover:not(:disabled) { background: #fef2f2; border-color: #ef4444; }
.mgmt-danger-btn:disabled { opacity: 0.4; cursor: not-allowed; }

/* ── 表头 通用 */
.mgmt-table-head {
  display: flex;
  align-items: center;
  padding: 0 16px;
  background: #f0f4f8;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: #64748b;
}
.mgmt-chk-col {
  width: 40px;
  padding: 11px 0;
  flex-shrink: 0;
  display: flex;
  align-items: center;
}
.mgmt-col {
  flex: 1;
  padding: 11px 10px;
  display: flex;
  align-items: center;
  min-width: 0;
}
.mgmt-col--id    { flex: 0 0 70px; }
.mgmt-col--user  { flex: 0 0 160px; }
.mgmt-col--email { flex: 2; }
.mgmt-col--role  { flex: 0 0 100px; }
.mgmt-col--status{ flex: 0 0 90px; }
.mgmt-col--date  { flex: 0 0 145px; }
.mgmt-col--action{ flex: 0 0 80px; justify-content: center; }

.mgmt-table-head--mat .mgmt-col { flex: 1; }

/* 操作审计：按内容收窄列，详情列吃剩余宽度 */
.mgmt-col-audit--idx {
  flex: 0 0 40px;
  max-width: 40px;
  justify-content: center;
  padding-left: 6px;
  padding-right: 6px;
}
.mgmt-col-audit--operator { flex: 0 0 118px; min-width: 0; }
.mgmt-col-audit--action { flex: 0 0 104px; min-width: 0; }
.mgmt-col-audit--target { flex: 0 0 84px; min-width: 0; }
.mgmt-col-audit--targetid { flex: 0 0 112px; min-width: 0; }
.mgmt-col-audit--detail {
  flex: 1 1 220px;
  min-width: 0;
  align-items: flex-start;
  padding-top: 10px;
  padding-bottom: 10px;
}
.mgmt-col-audit--ip { flex: 0 0 112px; min-width: 0; }
.mgmt-col-audit--time { flex: 0 0 144px; min-width: 0; }
.mgmt-row--audit .mgmt-col { padding: 8px 6px; }
.mgmt-table-head--audit .mgmt-col { padding: 9px 6px; }

/* Custom checkbox */
.mgmt-checkbox {
  width: 15px;
  height: 15px;
  border-radius: 4px;
  accent-color: #6366f1;
  cursor: pointer;
}

/* ── List */
.mgmt-list {
  display: flex;
  flex-direction: column;
  gap: 5px;
  min-height: 200px;
  position: relative;
}

/* ── Row 通用 */
.mgmt-row {
  display: flex;
  align-items: center;
  background: #ffffff;
  border: 1px solid #e8edf4;
  border-radius: 11px;
  overflow: hidden;
  transition: border-color 0.17s, box-shadow 0.17s, transform 0.14s;
  padding: 0 16px;
}
.mgmt-row:hover {
  border-color: #c7d2fe;
  box-shadow: 0 3px 14px rgba(99,102,241,0.09);
  transform: translateY(-1px);
}
.mgmt-row--disabled { opacity: 0.65; }
.mgmt-row--self { background: #f8faff; }
.mgmt-row--mat  { min-height: 52px; }
.mgmt-row--audit {
  min-height: 46px;
  align-items: flex-start;
}
.mgmt-row--audit .mgmt-col:not(.mgmt-col-audit--detail) {
  padding-top: 10px;
  padding-bottom: 10px;
}

/* ── Avatar */
.mgmt-avatar {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: #fff;
  font-size: 12px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  margin-right: 8px;
}
.mgmt-avatar--admin {
  background: linear-gradient(135deg, #f59e0b, #ef4444);
}
.mgmt-username {
  font-size: 13px;
  color: #1e293b;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.mgmt-email {
  font-size: 12.5px;
  color: #475569;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

/* ── Role / Status badges */
.mgmt-role-badge {
  font-size: 11.5px;
  font-weight: 600;
  padding: 3px 9px;
  border-radius: 20px;
  white-space: nowrap;
}
.mgmt-role--admin {
  background: rgba(245,158,11,0.12);
  color: #b45309;
  border: 1px solid rgba(245,158,11,0.3);
}
.mgmt-role--user {
  background: rgba(99,102,241,0.08);
  color: #4f46e5;
  border: 1px solid rgba(99,102,241,0.2);
}

.mgmt-status-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 12px;
  font-weight: 600;
  padding: 3px 9px;
  border-radius: 20px;
  white-space: nowrap;
}
.mgmt-status--on {
  background: rgba(16,185,129,0.1);
  color: #059669;
}
.mgmt-status--on .rec-status-dot { background: #10b981; box-shadow: 0 0 5px rgba(16,185,129,0.5); }
.mgmt-status--off {
  background: rgba(239,68,68,0.1);
  color: #dc2626;
}
.mgmt-status--off .rec-status-dot { background: #ef4444; }

/* ── Action buttons for user row */
.mgmt-action-btn {
  padding: 5px 14px;
  border-radius: 7px;
  font-size: 12.5px;
  font-weight: 600;
  border: 1.5px solid;
  cursor: pointer;
  transition: all 0.15s;
}
.mgmt-action-btn:disabled { opacity: 0.35; cursor: not-allowed; }
.mgmt-action-btn--ok {
  background: rgba(16,185,129,0.08);
  color: #059669;
  border-color: rgba(16,185,129,0.3);
}
.mgmt-action-btn--ok:hover:not(:disabled) { background: #d1fae5; border-color: #10b981; }
.mgmt-action-btn--danger {
  background: rgba(239,68,68,0.07);
  color: #dc2626;
  border-color: rgba(239,68,68,0.25);
}
.mgmt-action-btn--danger:hover:not(:disabled) { background: #fee2e2; border-color: #ef4444; }

/* ── Batch bar */
.mgmt-batch-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 18px;
  background: linear-gradient(90deg, #f0fdf4, #ecfdf5);
  border: 1.5px solid #6ee7b7;
  border-radius: 12px;
  gap: 12px;
  flex-wrap: wrap;
}
.mgmt-batch-info {
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 13.5px;
  color: #065f46;
  font-weight: 500;
}
.mgmt-batch-info b { color: #047857; font-weight: 700; }
.mgmt-batch-actions { display: flex; gap: 8px; }
.mgmt-batch-btn {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 6px 14px;
  border-radius: 8px;
  font-size: 12.5px;
  font-weight: 600;
  cursor: pointer;
  border: 1.5px solid;
  transition: all 0.15s;
}
.mgmt-batch-btn--ok {
  background: #d1fae5; color: #047857; border-color: #6ee7b7;
}
.mgmt-batch-btn--ok:hover { background: #a7f3d0; border-color: #34d399; }
.mgmt-batch-btn--err {
  background: #fee2e2; color: #b91c1c; border-color: #fca5a5;
}
.mgmt-batch-btn--err:hover { background: #fecaca; border-color: #f87171; }
.mgmt-batch-btn--ghost {
  background: #f1f5f9; color: #475569; border-color: #e2e8f0;
}
.mgmt-batch-btn--ghost:hover { background: #e2e8f0; }

.batch-slide-enter-active, .batch-slide-leave-active {
  transition: all 0.22s ease;
}
.batch-slide-enter-from, .batch-slide-leave-to {
  opacity: 0;
  transform: translateY(-8px);
}

/* ── 素材管理专属 */
.mat-storage-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  background: rgba(245,158,11,0.06);
  border: 1px solid rgba(245,158,11,0.2);
  border-radius: 10px;
  font-size: 13px;
  color: #92400e;
}
.mat-storage-label { color: #78350f; font-weight: 500; }
.mat-storage-value { font-weight: 700; font-family: 'Fira Code', monospace; color: #b45309; }

.mat-file-icon {
  width: 32px;
  height: 32px;
  border-radius: 7px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 9px;
  font-weight: 800;
  letter-spacing: 0.05em;
  flex-shrink: 0;
  margin-right: 10px;
  font-family: 'Fira Code', monospace;
}
.mat-file-icon--pdf  { background: #fee2e2; color: #dc2626; border: 1px solid #fecaca; }
.mat-file-icon--docx { background: #dbeafe; color: #1d4ed8; border: 1px solid #bfdbfe; }
.mat-file-icon--txt  { background: #f0fdf4; color: #15803d; border: 1px solid #bbf7d0; }

.mat-filename {
  font-size: 13px;
  font-weight: 500;
  color: #1e293b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.mat-type-badge {
  font-size: 11px;
  font-weight: 700;
  padding: 3px 8px;
  border-radius: 5px;
  font-family: 'Fira Code', monospace;
  background: #f1f5f9;
  color: #475569;
  border: 1px solid #e2e8f0;
  letter-spacing: 0.05em;
}

.mat-action-group {
  display: flex;
  gap: 5px;
}
.mat-act-btn {
  padding: 4px 10px;
  border-radius: 6px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  border: 1.5px solid;
  transition: all 0.14s;
  white-space: nowrap;
}
.mat-act-btn--view   { background: #eff6ff; color: #1d4ed8; border-color: #bfdbfe; }
.mat-act-btn--view:hover   { background: #dbeafe; }
.mat-act-btn--review { background: #fffbeb; color: #b45309; border-color: #fde68a; }
.mat-act-btn--review:hover { background: #fef3c7; }
.mat-act-btn--del    { background: #fff1f2; color: #be123c; border-color: #fecdd3; }
.mat-act-btn--del:hover    { background: #ffe4e6; }

/* ── 公告管理专属 */
.ann-publish-btn {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 22px;
  background: rgba(255,255,255,0.15);
  color: #fff;
  border: 1.5px solid rgba(255,255,255,0.3);
  border-radius: 12px;
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.18s;
  white-space: nowrap;
  backdrop-filter: blur(4px);
}
.ann-publish-btn:hover {
  background: rgba(255,255,255,0.25);
  border-color: rgba(255,255,255,0.5);
  transform: translateY(-1px);
}

.ann-list {
  display: flex;
  flex-direction: column;
  gap: 10px;
  min-height: 160px;
  position: relative;
}

.ann-card {
  display: flex;
  align-items: stretch;
  gap: 0;
  background: #fff;
  border: 1px solid #e8edf4;
  border-radius: 14px;
  overflow: hidden;
  transition: border-color 0.18s, box-shadow 0.18s, transform 0.14s;
  position: relative;
  cursor: default;
}
.ann-card:hover {
  border-color: #c4b5fd;
  box-shadow: 0 4px 16px rgba(139,92,246,0.1);
  transform: translateY(-1px);
}
.ann-card--pinned {
  border-left: 4px solid #f59e0b;
}
.ann-card--expired {
  opacity: 0.6;
}
.ann-card--expired .ann-title { color: #64748b; }

.ann-pin-badge {
  position: absolute;
  top: 10px;
  right: 136px;
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 10.5px;
  font-weight: 700;
  color: #b45309;
  background: #fef3c7;
  border: 1px solid #fde68a;
  padding: 2px 8px;
  border-radius: 20px;
}

.ann-card__body {
  flex: 1;
  padding: 16px 20px;
  min-width: 0;
}
.ann-card__top {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 6px;
  flex-wrap: wrap;
}
.ann-status-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11.5px;
  font-weight: 600;
  padding: 3px 9px;
  border-radius: 20px;
  white-space: nowrap;
  flex-shrink: 0;
}
.ann-status--active {
  background: rgba(16,185,129,0.1);
  color: #059669;
}
.ann-status--active .rec-status-dot { background: #10b981; animation: rec-dot-pulse 2s ease-in-out infinite; }
.ann-status--expired {
  background: rgba(100,116,139,0.1);
  color: #64748b;
}
.ann-status--expired .rec-status-dot { background: #94a3b8; }
.ann-status--pending {
  background: rgba(245,158,11,0.1);
  color: #d97706;
}
.ann-status--pending .rec-status-dot { background: #f59e0b; animation: rec-dot-pulse 1.5s ease-in-out infinite; }

.ann-title {
  font-size: 14.5px;
  font-weight: 700;
  color: #0f172a;
  margin: 0;
  line-height: 1.3;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.ann-content {
  font-size: 13px;
  color: #475569;
  margin: 0 0 10px;
  line-height: 1.5;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}
.ann-meta {
  display: flex;
  gap: 20px;
  flex-wrap: wrap;
}
.ann-meta-item {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 11.5px;
  color: #94a3b8;
  font-family: 'Fira Code', monospace;
}

.ann-card__actions {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 14px 16px;
  flex-shrink: 0;
  border-left: 1px solid #f1f5f9;
  justify-content: center;
}
.ann-act-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 7px 14px;
  border-radius: 8px;
  font-size: 12.5px;
  font-weight: 600;
  cursor: pointer;
  border: 1.5px solid;
  transition: all 0.14s;
  white-space: nowrap;
  min-width: 76px;
  justify-content: center;
}
.ann-act-btn--edit  { background: #f0f4ff; color: #4f46e5; border-color: #c7d2fe; }
.ann-act-btn--edit:hover  { background: #e0e7ff; border-color: #a5b4fc; }
.ann-act-btn--del   { background: #fff1f2; color: #be123c; border-color: #fecdd3; }
.ann-act-btn--del:hover   { background: #ffe4e6; border-color: #fda4af; }

/* ── 操作审计日志专属 */
.audit-idx {
  font-size: 11px;
  color: #94a3b8;
  font-family: 'Fira Code', monospace;
}
.audit-action-badge {
  display: inline-flex;
  align-items: center;
  font-size: 11.5px;
  font-weight: 600;
  padding: 3px 9px;
  border-radius: 6px;
  white-space: nowrap;
}
.audit-action--danger  { background: #fee2e2; color: #dc2626; border: 1px solid #fecaca; }
.audit-action--warning { background: #fffbeb; color: #b45309; border: 1px solid #fde68a; }
.audit-action--success { background: #f0fdf4; color: #15803d; border: 1px solid #bbf7d0; }
.audit-action--info    { background: #f0f9ff; color: #0369a1; border: 1px solid #bae6fd; }

.audit-target-chip {
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  background: #f0f4f8;
  color: #475569;
  border-radius: 4px;
  border: 1px solid #e2e8f0;
  font-family: 'Fira Code', monospace;
  letter-spacing: 0.04em;
}
.audit-id-text {
  font-size: 11px;
  color: #64748b;
  font-family: 'Fira Code', monospace;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}
.audit-detail-text {
  font-size: 12px;
  line-height: 1.55;
  color: #334155;
  width: 100%;
  max-width: 100%;
  white-space: pre-wrap;
  word-break: break-word;
  overflow-wrap: anywhere;
  max-height: 200px;
  overflow-y: auto;
  border-radius: 6px;
  padding: 8px 10px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  box-sizing: border-box;
  font-family: ui-monospace, 'SFMono-Regular', Menlo, Consolas, 'Liberation Mono', monospace;
  scrollbar-width: thin;
  scrollbar-color: #cbd5e1 #f1f5f9;
}
.audit-detail-text--empty {
  max-height: none;
  overflow: visible;
  padding: 0;
  background: transparent;
  border: none;
  color: #94a3b8;
  font-family: inherit;
}
.audit-detail-text::-webkit-scrollbar {
  width: 6px;
}
.audit-detail-text::-webkit-scrollbar-thumb {
  background: #cbd5e1;
  border-radius: 3px;
}
.audit-ip {
  font-size: 11px;
  color: #94a3b8;
  font-family: 'Fira Code', monospace;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}

/* ── 空状态 ── */
.ins-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 64px 32px;
  text-align: center;
}
.ins-empty-icon { width: 80px; height: 80px; }
.ins-empty-title { font-size: 16px; font-weight: 600; color: #334155; margin: 0; }
.ins-empty-sub   { font-size: 13px; color: #94a3b8; margin: 0; }

/* ── 批量操作栏 ──────────────────────────────────────────────────────────── */
.batch-action-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 16px;
  background: linear-gradient(90deg, #eff6ff, #f0f9ff);
  border: 1px solid #bfdbfe;
  border-radius: 10px;
  margin-bottom: 2px;
}
.batch-count {
  font-size: 13px;
  color: #1d4ed8;
  font-weight: 500;
}
.batch-count b {
  font-size: 15px;
  font-weight: 700;
}
.batch-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

/* 进入/离开动画 */
.batch-bar-slide-enter-active,
.batch-bar-slide-leave-active {
  transition: all 0.25s ease;
  overflow: hidden;
}
.batch-bar-slide-enter-from,
.batch-bar-slide-leave-to {
  opacity: 0;
  transform: translateY(-6px);
  max-height: 0;
  padding-top: 0;
  padding-bottom: 0;
}
.batch-bar-slide-enter-to,
.batch-bar-slide-leave-from {
  max-height: 60px;
}

/* ── 旧的 dark insight-card（数据看板用，保持不变） ── */
.insight-card {
  padding: var(--space-lg);
  border-radius: var(--radius-card);
  background: rgba(15, 23, 42, 0.9);
  color: #e2e8f0;
  box-shadow: var(--shadow-card);
}

.insight-card.accent {
  background: linear-gradient(135deg, #0EA5E9, #38BDF8);
}

.insight-card h3 {
  margin-bottom: 10px;
}

.insight-card ul {
  margin-top: 12px;
  padding-left: 18px;
}

.insight-card li {
  margin-bottom: 6px;
}

.insight-tags {
  margin-top: 14px;
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.insight-tags span {
  padding: 4px 10px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.2);
  font-size: 0.75rem;
}

/* ── 偏好洞察 Hero 右侧操作区 ─────────────────────────────────────────────── */
.ins-hero__actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.ins-range-select {
  width: 148px;
}
.ins-range-select :deep(.el-input__wrapper) {
  background: rgba(255,255,255,0.12);
  border-color: rgba(255,255,255,0.25);
  color: #fff;
  box-shadow: none;
}
.ins-range-select :deep(.el-input__inner) {
  color: #e0f2fe;
  font-size: 13px;
}
.ins-range-select :deep(.el-select__caret) {
  color: #bae6fd;
}

/* 图表卡右上角图表类型切换 */
.ins-chart-type-select {
  margin-left: auto;
  flex-shrink: 0;
}
.ins-chart-type-select .el-select {
  width: 86px;
}

.records-header {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
}

.records-controls {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.records-table-card {
  border-radius: var(--radius-card);
  overflow: hidden;
  box-shadow: var(--shadow-card);
  border: 1px solid rgba(0, 0, 0, 0.06);
}

.records-table-card :deep(.el-table tbody tr:hover) {
  background: rgba(14, 165, 233, 0.04);
}

.pagination-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 8px 0;
  color: #64748b;
}

/* ══ 生成记录 — 全新设计 ═══════════════════════════════════════════════════ */
.rec-shell {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

/* Hero */
.rec-hero {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 20px;
  flex-wrap: wrap;
  padding: 28px 32px 22px;
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 60%, #1a2744 100%);
  border-radius: 18px;
  border: 1px solid rgba(99, 102, 241, 0.2);
  box-shadow: 0 8px 32px rgba(15, 23, 42, 0.18), inset 0 1px 0 rgba(255,255,255,0.06);
  position: relative;
  overflow: hidden;
}
.rec-hero::before {
  content: '';
  position: absolute;
  top: -60px; right: -60px;
  width: 220px; height: 220px;
  background: radial-gradient(circle, rgba(99,102,241,0.18) 0%, transparent 70%);
  pointer-events: none;
}
.rec-hero__eyebrow {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.18em;
  color: #6366f1;
  text-transform: uppercase;
  margin-bottom: 6px;
  font-family: 'Fira Code', monospace;
}
.rec-hero__title {
  font-size: 26px;
  font-weight: 700;
  color: #f8fafc;
  line-height: 1.15;
  margin: 0 0 6px;
  letter-spacing: -0.01em;
}
.rec-hero__sub {
  font-size: 13px;
  color: #64748b;
  margin: 0;
  font-family: 'Fira Code', monospace;
}
.rec-hero__stats {
  display: flex;
  gap: 10px;
  flex-shrink: 0;
}
.rec-stat-pill {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 12px 20px;
  border-radius: 12px;
  min-width: 72px;
}
.rec-stat-pill--all { background: rgba(99,102,241,0.15); border: 1px solid rgba(99,102,241,0.3); }
.rec-stat-pill--ok  { background: rgba(16,185,129,0.12); border: 1px solid rgba(16,185,129,0.3); }
.rec-stat-pill--err { background: rgba(239,68,68,0.12);  border: 1px solid rgba(239,68,68,0.3); }
.rec-stat-num {
  font-size: 22px;
  font-weight: 700;
  color: #f1f5f9;
  line-height: 1;
  font-family: 'Fira Code', monospace;
}
.rec-stat-label { font-size: 11px; color: #64748b; }

/* Toolbar */
.rec-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  padding: 14px 16px;
  background: #ffffff;
  border: 1px solid #e8edf4;
  border-radius: 14px;
  box-shadow: 0 1px 6px rgba(15,23,42,0.05);
}
.rec-search-wrap {
  position: relative;
  display: flex;
  align-items: center;
  flex: 1;
  min-width: 200px;
}
.rec-search-icon {
  position: absolute;
  left: 12px;
  color: #94a3b8;
  width: 16px;
  height: 16px;
  pointer-events: none;
}
.rec-search-input {
  width: 100%;
  padding: 9px 36px 9px 38px;
  border: 1.5px solid #e2e8f0;
  border-radius: 10px;
  font-size: 13.5px;
  color: #1e293b;
  background: #f8fafc;
  outline: none;
  transition: border-color 0.18s, box-shadow 0.18s, background 0.18s;
  font-family: inherit;
}
.rec-search-input::placeholder { color: #94a3b8; }
.rec-search-input:focus {
  border-color: #6366f1;
  background: #fff;
  box-shadow: 0 0 0 3px rgba(99,102,241,0.1);
}
.rec-search-clear {
  position: absolute;
  right: 10px;
  display: flex;
  align-items: center;
  background: none;
  border: none;
  color: #94a3b8;
  cursor: pointer;
  padding: 2px;
  border-radius: 4px;
  transition: color 0.15s;
}
.rec-search-clear:hover { color: #475569; }

.rec-filter-chips {
  display: flex;
  gap: 6px;
  flex-shrink: 0;
}
.rec-chip {
  padding: 6px 14px;
  border-radius: 20px;
  font-size: 12.5px;
  font-weight: 500;
  border: 1.5px solid #e2e8f0;
  background: #f8fafc;
  color: #475569;
  cursor: pointer;
  transition: all 0.17s;
  white-space: nowrap;
}
.rec-chip:hover { border-color: #6366f1; color: #6366f1; background: rgba(99,102,241,0.06); }
.rec-chip--active.rec-chip--all      { background: #6366f1; border-color: #6366f1; color: #fff; }
.rec-chip--active.rec-chip--completed { background: #10b981; border-color: #10b981; color: #fff; }
.rec-chip--active.rec-chip--pending  { background: #f59e0b; border-color: #f59e0b; color: #fff; }
.rec-chip--active.rec-chip--failed   { background: #ef4444; border-color: #ef4444; color: #fff; }

.rec-datepicker {
  flex-shrink: 0;
}
.rec-datepicker :deep(.el-input__wrapper) {
  border-radius: 10px;
  border: 1.5px solid #e2e8f0;
  box-shadow: none;
  background: #f8fafc;
}
.rec-datepicker :deep(.el-input__wrapper:hover) { border-color: #6366f1; }

.rec-export-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 9px 18px;
  background: #0f172a;
  color: #f1f5f9;
  border: none;
  border-radius: 10px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
  transition: background 0.18s, transform 0.12s;
  flex-shrink: 0;
}
.rec-export-btn:hover { background: #1e293b; transform: translateY(-1px); }
.rec-export-btn:active { transform: translateY(0); }

/* AI 索引管理按钮组 */
.rec-ai-index-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

.rec-ai-index-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 9px 16px;
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  color: #fff;
  border: none;
  border-radius: 10px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
  transition: opacity 0.2s, transform 0.12s, box-shadow 0.2s;
  flex-shrink: 0;
  box-shadow: 0 2px 8px rgba(79, 70, 229, 0.35);
}
.rec-ai-index-btn:hover:not(:disabled) {
  opacity: 0.9;
  transform: translateY(-1px);
  box-shadow: 0 4px 14px rgba(79, 70, 229, 0.45);
}
.rec-ai-index-btn:active:not(:disabled) { transform: translateY(0); }
.rec-ai-index-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
  box-shadow: none;
}
.rec-ai-index-btn--running {
  background: linear-gradient(135deg, #64748b 0%, #475569 100%);
  box-shadow: none;
}

.rec-ai-status-pill {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 5px 11px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: 500;
  white-space: nowrap;
}
.rec-ai-status-pill--ok {
  background: rgba(16, 185, 129, 0.12);
  color: #059669;
}
.rec-ai-status-pill--off {
  background: rgba(239, 68, 68, 0.1);
  color: #dc2626;
}
.rec-ai-status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  flex-shrink: 0;
}
.rec-ai-status-pill--ok .rec-ai-status-dot {
  background: #10b981;
  box-shadow: 0 0 5px rgba(16, 185, 129, 0.6);
}
.rec-ai-status-pill--off .rec-ai-status-dot {
  background: #ef4444;
}

@keyframes spin { to { transform: rotate(360deg); } }
.spin-icon { animation: spin 1s linear infinite; }

/* Table Head */
.rec-table-head {
  display: grid;
  grid-template-columns: 72px 1fr 150px 130px 70px 110px 155px;
  gap: 0;
  padding: 0 20px 0 32px;
  background: #f0f4f8;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: #64748b;
}
.rec-col {
  padding: 11px 10px;
  display: flex;
  align-items: center;
}
.rec-col--id     { padding-left: 0; }
.rec-col--title  { flex: 1; }

/* List */
.rec-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-height: 240px;
  position: relative;
}

/* Empty */
.rec-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 64px 24px;
  color: #94a3b8;
  font-size: 14px;
}
.rec-empty-icon { opacity: 0.5; }
.rec-reset-link {
  background: none;
  border: none;
  color: #6366f1;
  font-size: 13px;
  cursor: pointer;
  text-decoration: underline;
  font-family: inherit;
}

/* Row */
.rec-row {
  display: grid;
  grid-template-columns: 8px 72px 1fr 150px 130px 70px 110px 155px;
  gap: 0;
  align-items: center;
  background: #ffffff;
  border: 1px solid #e8edf4;
  border-radius: 12px;
  overflow: hidden;
  transition: border-color 0.18s, box-shadow 0.18s, transform 0.15s;
  cursor: default;
}
.rec-row:hover {
  border-color: #c7d2fe;
  box-shadow: 0 4px 16px rgba(99,102,241,0.1);
  transform: translateY(-1px);
}
.rec-row--completed .rec-row__accent { background: #10b981; }
.rec-row--failed    .rec-row__accent { background: #ef4444; }
.rec-row--pending   .rec-row__accent { background: #f59e0b; }
.rec-row--queued    .rec-row__accent { background: #f59e0b; }
.rec-row--processing .rec-row__accent { background: #6366f1; }
.rec-row__accent {
  width: 4px;
  height: 100%;
  align-self: stretch;
  border-radius: 12px 0 0 12px;
  flex-shrink: 0;
}

.rec-id-badge {
  font-size: 11px;
  font-weight: 600;
  color: #64748b;
  background: #f1f5f9;
  padding: 3px 7px;
  border-radius: 6px;
  font-family: 'Fira Code', monospace;
}

.rec-title-main {
  font-size: 13.5px;
  font-weight: 600;
  color: #1e293b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
  line-height: 1.3;
}

.rec-avatar {
  width: 26px;
  height: 26px;
  border-radius: 50%;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: #fff;
  font-size: 11px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  margin-right: 7px;
}
.rec-username {
  font-size: 13px;
  color: #334155;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100px;
}

.rec-model-tag {
  font-size: 11.5px;
  background: #f0f4ff;
  color: #4f46e5;
  border: 1px solid #c7d2fe;
  border-radius: 6px;
  padding: 3px 8px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
  font-family: 'Fira Code', monospace;
}

.rec-pages {
  font-size: 14px;
  font-weight: 600;
  color: #334155;
  font-family: 'Fira Code', monospace;
}

.rec-status-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 12px;
  font-weight: 600;
  padding: 4px 10px;
  border-radius: 20px;
  white-space: nowrap;
}
.rec-status-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex-shrink: 0;
}
.rec-status--completed {
  background: rgba(16,185,129,0.1);
  color: #059669;
}
.rec-status--completed .rec-status-dot { background: #10b981; box-shadow: 0 0 6px rgba(16,185,129,0.6); animation: rec-dot-pulse 2s ease-in-out infinite; }
.rec-status--failed {
  background: rgba(239,68,68,0.1);
  color: #dc2626;
}
.rec-status--failed .rec-status-dot { background: #ef4444; }
.rec-status--pending,
.rec-status--queued {
  background: rgba(245,158,11,0.1);
  color: #d97706;
}
.rec-status--pending .rec-status-dot,
.rec-status--queued .rec-status-dot { background: #f59e0b; animation: rec-dot-pulse 1.2s ease-in-out infinite; }
.rec-status--processing {
  background: rgba(99,102,241,0.1);
  color: #4f46e5;
}
.rec-status--processing .rec-status-dot { background: #6366f1; animation: rec-dot-pulse 0.8s ease-in-out infinite; }

@keyframes rec-dot-pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50%       { opacity: 0.4; transform: scale(0.7); }
}

.rec-time {
  font-size: 12px;
  color: #64748b;
  font-family: 'Fira Code', monospace;
}

/* Footer / Pagination */
.rec-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
  padding: 14px 4px 4px;
}
.rec-footer__count {
  font-size: 13px;
  color: #64748b;
}
.rec-footer__count b { color: #1e293b; }
.rec-pagination {
  display: flex;
  align-items: center;
  gap: 4px;
}
.rec-page-btn {
  width: 34px;
  height: 34px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 8px;
  border: 1.5px solid #e2e8f0;
  background: #fff;
  color: #475569;
  cursor: pointer;
  transition: all 0.15s;
  font-size: 13px;
  font-weight: 500;
}
.rec-page-btn--num { font-family: 'Fira Code', monospace; }
.rec-page-btn:hover:not(:disabled):not(.rec-page-btn--active) {
  border-color: #6366f1;
  color: #6366f1;
  background: rgba(99,102,241,0.06);
}
.rec-page-btn--active {
  background: #6366f1;
  border-color: #6366f1;
  color: #fff;
  cursor: default;
}
.rec-page-btn:disabled { opacity: 0.35; cursor: not-allowed; }
.rec-page-ellipsis {
  width: 34px;
  height: 34px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #94a3b8;
  font-size: 14px;
}

.placeholder-section {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 320px;
}

.placeholder-card {
  padding: 28px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.9);
  text-align: center;
  box-shadow: 0 18px 32px rgba(15, 23, 42, 0.1);
}

/* =========================================================
   系统配置中心
   ========================================================= */
.settings-section {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.settings-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
}

.settings-title-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.settings-icon-wrap {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: linear-gradient(135deg, #6366f1, #818cf8);
  display: flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  flex-shrink: 0;
}

.settings-title {
  font-size: 1.35rem;
  font-weight: 700;
  color: #1e293b;
  margin: 0;
}

.settings-subtitle {
  font-size: 0.85rem;
  color: #94a3b8;
  margin: 4px 0 0 46px;
}

.settings-header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.settings-refresh-btn {
  width: 34px;
  height: 34px;
  border-radius: 8px;
  border: 1.5px solid #e2e8f0;
  background: #fff;
  color: #64748b;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.18s;
}
.settings-refresh-btn:hover { border-color: #6366f1; color: #6366f1; }
.settings-refresh-btn.spinning svg { animation: spin-kf 1s linear infinite; }

@keyframes spin-kf {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.spin-svg { animation: spin-kf 1s linear infinite; }

/* Tab 栏 */
.settings-tabs {
  display: flex;
  gap: 6px;
  background: #f1f5f9;
  padding: 5px;
  border-radius: 12px;
  width: fit-content;
}

.settings-tab-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 16px;
  border-radius: 9px;
  border: none;
  background: transparent;
  color: #64748b;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.18s;
}

.settings-tab-btn:hover { color: #6366f1; }

.settings-tab-btn.active {
  background: #fff;
  color: #6366f1;
  font-weight: 600;
  box-shadow: 0 2px 8px rgba(0,0,0,0.08);
}

.stab-icon {
  display: flex;
  align-items: center;
}

/* 加载状态 */
.settings-loading {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 48px 0;
  justify-content: center;
  color: #94a3b8;
  font-size: 0.9rem;
}

/* 表单卡片 */
.settings-form-card {
  background: #fff;
  border: 1.5px solid #e8edf5;
  border-radius: 16px;
  overflow: hidden;
}

.settings-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
  padding: 18px 24px;
  border-bottom: 1px solid #f1f5f9;
  transition: background 0.15s;
}

.settings-row:last-of-type { border-bottom: none; }
.settings-row:hover { background: #fafbff; }

.settings-row-info {
  flex: 1;
  min-width: 0;
}

.settings-row-label {
  font-size: 0.9rem;
  font-weight: 600;
  color: #1e293b;
  margin-bottom: 3px;
}

.settings-row-desc {
  font-size: 0.8rem;
  color: #94a3b8;
  line-height: 1.45;
}

.settings-row-ctrl {
  flex-shrink: 0;
}

.settings-updated-row {
  padding: 10px 24px;
  font-size: 0.78rem;
  color: #94a3b8;
  border-top: 1px solid #f1f5f9;
  min-height: 36px;
}

/* 页脚操作栏 */
.settings-footer {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 24px;
  background: #f8fafc;
  border-top: 1px solid #e9eef5;
}

.settings-save-btn {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 9px 22px;
  border-radius: 10px;
  border: none;
  background: linear-gradient(135deg, #6366f1, #818cf8);
  color: #fff;
  font-size: 0.875rem;
  font-weight: 600;
  cursor: pointer;
  box-shadow: 0 2px 10px rgba(99,102,241,0.3);
  transition: all 0.18s;
}
.settings-save-btn:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 14px rgba(99,102,241,0.4);
}
.settings-save-btn:disabled { opacity: 0.55; cursor: not-allowed; transform: none !important; }

.settings-reset-btn {
  padding: 8px 16px;
  border-radius: 10px;
  border: 1.5px solid #e2e8f0;
  background: #fff;
  color: #64748b;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.18s;
}
.settings-reset-btn:hover { border-color: #94a3b8; color: #475569; }

.settings-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 60px 0;
  color: #94a3b8;
  font-size: 0.9rem;
}

@media (max-width: 1200px) {
  .admin-shell {
    grid-template-columns: 220px 1fr;
  }
}

@media (max-width: 960px) {
  .admin-shell {
    grid-template-columns: 1fr;
  }

  .admin-sidebar {
    flex-direction: row;
    flex-wrap: wrap;
    gap: 16px;
  }

  .admin-nav {
    flex-direction: row;
    flex-wrap: wrap;
  }

  .nav-item {
    flex: 1 1 140px;
  }
}

@media (max-width: 720px) {
  .admin-main {
    padding: 24px;
  }

  .admin-topbar {
    flex-direction: column;
  }

  .overview-header {
    flex-direction: column;
    align-items: flex-start;
  }

  .records-controls {
    width: 100%;
  }
}

/* ── 素材统计卡片 ─────────────────────────────────────────────────────────── */
.material-stats-row {
  display: flex;
  gap: 16px;
  margin-bottom: 20px;
  flex-wrap: wrap;
}

.mstat-card {
  flex: 1;
  min-width: 120px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.06);
}

.mstat-card.success { border-left: 3px solid #10b981; }
.mstat-card.warning { border-left: 3px solid #f59e0b; }
.mstat-card.danger  { border-left: 3px solid #ef4444; }

.mstat-label {
  font-size: 12px;
  color: #64748b;
  font-weight: 500;
}

.mstat-value {
  font-size: 22px;
  font-weight: 700;
  color: #0f172a;
  line-height: 1.2;
}

/* ── 素材详情抽屉 ─────────────────────────────────────────────────────────── */
.drawer-body {
  padding: 0 4px;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.drawer-loading {
  padding: 24px;
}

.drawer-section {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.drawer-section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.drawer-section-title {
  font-size: 14px;
  font-weight: 600;
  color: #0f172a;
  margin: 0;
  padding-bottom: 4px;
  border-bottom: 2px solid #e2e8f0;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 11px;
  color: #475569;
}

.review-hint {
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 12px 16px;
  font-size: 13px;
  color: #64748b;
}
.review-hint.warning { background: #fffbeb; border-color: #fcd34d; color: #92400e; }
.review-hint.info    { background: #eff6ff; border-color: #93c5fd; color: #1d4ed8; }

.review-result-card {
  border-radius: 10px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  border: 1.5px solid #e2e8f0;
  background: #f8fafc;
}
.review-result-card.pass      { border-color: #6ee7b7; background: #f0fdf4; }
.review-result-card.violation { border-color: #fca5a5; background: #fff5f5; }
.review-result-card.unknown   { border-color: #fcd34d; background: #fffbeb; }

.review-badge {
  font-size: 15px;
  font-weight: 700;
}
.badge-pass      { color: #059669; }
.badge-violation { color: #dc2626; }
.badge-unknown   { color: #d97706; }

.review-reason {
  font-size: 13px;
  color: #374151;
  line-height: 1.6;
  margin: 0;
}

.review-time {
  font-size: 11px;
  color: #94a3b8;
  margin: 0;
}

.review-action {
  margin-top: 4px;
}

.content-preview {
  background: #0f172a;
  border-radius: 8px;
  padding: 14px;
  max-height: 320px;
  overflow-y: auto;
}

.content-text {
  font-family: 'JetBrains Mono', 'Fira Code', 'Courier New', monospace;
  font-size: 12px;
  color: #e2e8f0;
  white-space: pre-wrap;
  word-break: break-word;
  margin: 0;
  line-height: 1.7;
}

/* ── 文件预览 Tab ─────────────────────────────────────────────────────── */
.drawer-tabs {
  margin-top: 4px;
}

.file-preview-toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 12px;
}

.file-preview-tip {
  font-size: 12px;
  color: #94a3b8;
}

.file-preview-frame-wrap {
  position: relative;
  width: 100%;
  height: 560px;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  overflow: hidden;
  background: #f8fafc;
}

.file-preview-frame {
  width: 100%;
  height: 100%;
  border: none;
  display: block;
}

.preview-fallback {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  background: #f8fafc;
  color: #64748b;
  font-size: 14px;
}

.file-preview-fallback {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;
  padding: 60px 20px;
  background: #f8fafc;
  border: 1px dashed #e2e8f0;
  border-radius: 8px;
  color: #64748b;
  font-size: 14px;
  text-align: center;
}

/* ── 操作审计日志 ──────────────────────────────────────────────────────────── */
.audit-filter-bar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
  padding: 14px 16px;
  background: #f8fafc;
  border: 1px solid #e8edf2;
  border-radius: 10px;
  margin-bottom: 4px;
}

.audit-target-type {
  display: inline-block;
  padding: 1px 7px;
  border-radius: 4px;
  background: #f0f9ff;
  color: #0369a1;
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.5px;
}

/* ── 模板管理面板 ────────────────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════════════════
   模板管理 — 全新设计：深色精密风格
   色系：深石板 + 琥珀强调 + 靛蓝高亮
   字体：系统等宽用于数据，无衬线用于文本
═══════════════════════════════════════════════════════════════════════ */

/* ── 自旋动画 ─────────────────────────────────────────────────────── */
@keyframes spin { to { transform: rotate(360deg); } }
.spin { animation: spin 0.7s linear infinite; transform-origin: center; }

/* ── 区块根 ───────────────────────────────────────────────────────── */
.tmpl-mgr-section {
  padding: 0 2px;
}

/* ── 顶栏 ─────────────────────────────────────────────────────────── */
.tmpl-top-bar {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 16px;
  margin-bottom: 20px;
  padding-bottom: 20px;
  border-bottom: 1px solid #e8ecf2;
}

.tmpl-top-left {}

.tmpl-top-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
}

.tmpl-title-icon { color: #6366f1; }

.tmpl-mgr-title {
  font-size: 17px;
  font-weight: 700;
  color: #0f172a;
  margin: 0;
  letter-spacing: -0.3px;
}

.tmpl-mgr-subtitle {
  font-size: 12.5px;
  color: #94a3b8;
  margin: 0;
}

/* KPI 徽章行 */
.tmpl-kpi-row {
  display: flex;
  align-items: center;
  gap: 0;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  overflow: hidden;
}

.tmpl-kpi {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 10px 20px;
}

.tmpl-kpi-divider {
  width: 1px;
  height: 32px;
  background: #e2e8f0;
}

.tmpl-kpi-num {
  font-size: 22px;
  font-weight: 700;
  color: #1e293b;
  line-height: 1;
  font-variant-numeric: tabular-nums;
  font-family: 'Fira Code', 'Cascadia Code', monospace;
}

.tmpl-kpi-label {
  font-size: 10.5px;
  color: #94a3b8;
  margin-top: 3px;
  letter-spacing: 0.04em;
  text-transform: uppercase;
}

.tmpl-kpi--live .tmpl-kpi-num { color: #16a34a; }
.tmpl-kpi--off  .tmpl-kpi-num { color: #94a3b8; }

/* ── 子标签 ───────────────────────────────────────────────────────── */
.tmpl-tab-rail {
  display: flex;
  gap: 6px;
  margin-bottom: 20px;
}

.tmpl-tab-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 16px;
  font-size: 13px;
  font-weight: 500;
  color: #64748b;
  background: #f1f5f9;
  border: 1.5px solid transparent;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.15s;
}
.tmpl-tab-btn:hover { color: #334155; background: #e9eef5; }
.tmpl-tab-btn.is-active {
  color: #4f46e5;
  background: #eef2ff;
  border-color: #c7d2fe;
  font-weight: 600;
}
.tmpl-tab-btn--op.is-active {
  color: #0078d4;
  background: #eff6ff;
  border-color: #bfdbfe;
}

.op-favicon { width: 14px; height: 14px; border-radius: 3px; }

/* ── 本地管理面板 ─────────────────────────────────────────────────── */
.tmpl-local-panel {}

/* 工具栏 */
.tmpl-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 18px;
  flex-wrap: wrap;
}

.tmpl-search-wrap {
  position: relative;
  display: flex;
  align-items: center;
  flex: 1;
  min-width: 180px;
  max-width: 280px;
}

.tmpl-search-icon {
  position: absolute;
  left: 10px;
  color: #94a3b8;
  pointer-events: none;
}

.tmpl-search-input {
  width: 100%;
  padding: 7px 12px 7px 32px;
  font-size: 13px;
  background: #fff;
  border: 1.5px solid #e2e8f0;
  border-radius: 8px;
  color: #0f172a;
  outline: none;
  transition: border-color 0.15s;
}
.tmpl-search-input::placeholder { color: #cbd5e1; }
.tmpl-search-input:focus { border-color: #a5b4fc; box-shadow: 0 0 0 3px rgba(165,180,252,0.15); }

.tmpl-refresh-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 14px;
  font-size: 13px;
  font-weight: 500;
  color: #475569;
  background: #fff;
  border: 1.5px solid #e2e8f0;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.15s;
}
.tmpl-refresh-btn:hover:not(:disabled) { border-color: #a5b4fc; color: #4f46e5; }
.tmpl-refresh-btn:disabled { opacity: 0.5; cursor: not-allowed; }

/* 骨架屏 */
.tmpl-skeleton-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 16px;
}
.tmpl-skeleton-card {
  background: #fff;
  border: 1.5px solid #f1f5f9;
  border-radius: 14px;
  overflow: hidden;
}
.sk-thumb {
  height: 150px;
  background: linear-gradient(90deg, #f1f5f9 25%, #e8edf5 50%, #f1f5f9 75%);
  background-size: 400% 100%;
  animation: shimmer 1.4s infinite;
}
.sk-body { padding: 14px; display: flex; flex-direction: column; gap: 8px; }
.sk-line {
  border-radius: 4px;
  background: linear-gradient(90deg, #f1f5f9 25%, #e8edf5 50%, #f1f5f9 75%);
  background-size: 400% 100%;
  animation: shimmer 1.4s infinite;
}
.sk-line--title { height: 14px; width: 70%; }
.sk-line--sub   { height: 11px; width: 45%; }
.sk-line--tags  { height: 20px; width: 55%; }
@keyframes shimmer { to { background-position: -400% 0; } }

/* 空态 */
.tmpl-empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 70px 0;
}
.tmpl-empty-icon {
  width: 72px; height: 72px;
  background: #f8fafc;
  border-radius: 50%;
  display: flex; align-items: center; justify-content: center;
  color: #cbd5e1;
}
.tmpl-empty-text { font-size: 15px; font-weight: 600; color: #94a3b8; margin: 0; }
.tmpl-empty-sub  { font-size: 13px; color: #cbd5e1; margin: 0; }

/* 卡片网格 */
.tmpl-mgr-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 16px;
}

.tmpl-mgr-card {
  background: #fff;
  border-radius: 14px;
  border: 1.5px solid #e8ecf2;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  transition: box-shadow 0.2s, transform 0.15s, border-color 0.2s;
  position: relative;
}
.tmpl-mgr-card:hover {
  box-shadow: 0 8px 28px rgba(15,23,42,0.09);
  transform: translateY(-2px);
}
.tmpl-mgr-card.is-listed  { border-color: #bbf7d0; }
.tmpl-mgr-card.is-unlisted { opacity: 0.82; }

/* 预览图 */
.tmpl-card-thumb {
  position: relative;
  height: 150px;
  background: #f8fafc;
  overflow: hidden;
}

.tmpl-preview-img {
  width: 100%; height: 100%;
  object-fit: cover;
  transition: transform 0.3s;
}
.tmpl-mgr-card:hover .tmpl-preview-img { transform: scale(1.03); }

.tmpl-preview-placeholder {
  display: flex; align-items: center; justify-content: center;
  height: 100%;
  background: repeating-linear-gradient(
    45deg, #f8fafc, #f8fafc 8px, #f1f5f9 8px, #f1f5f9 16px
  );
}

/* 状态条 */
.tmpl-status-strip {
  position: absolute;
  bottom: 0; left: 0; right: 0;
  display: flex; align-items: center; gap: 5px;
  padding: 4px 10px;
  font-size: 11px; font-weight: 600;
  letter-spacing: 0.04em;
  backdrop-filter: blur(4px);
}
.strip--live {
  background: rgba(22, 163, 74, 0.88);
  color: #fff;
}
.strip--off {
  background: rgba(100, 116, 139, 0.82);
  color: #fff;
}
.strip-dot {
  width: 5px; height: 5px;
  border-radius: 50%;
  background: currentColor;
  opacity: 0.9;
}
.strip--live .strip-dot {
  animation: blink 1.4s ease-in-out infinite;
}
@keyframes blink { 0%,100% { opacity: 1; } 50% { opacity: 0.3; } }

/* 卡片信息 */
.tmpl-card-body {
  flex: 1;
  padding: 13px 14px 10px;
  display: flex; flex-direction: column; gap: 5px;
}

.tmpl-card-name {
  font-size: 13.5px;
  font-weight: 650;
  color: #0f172a;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  line-height: 1.4;
}

.tmpl-card-meta {
  display: flex;
  align-items: center;
  gap: 8px;
}

.tmpl-card-provider {
  font-size: 11.5px;
  color: #64748b;
}

.tmpl-card-id {
  font-size: 10px;
  color: #94a3b8;
  font-family: 'Fira Code', monospace;
  background: #f1f5f9;
  padding: 1px 5px;
  border-radius: 3px;
}

.tmpl-card-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.tmpl-tag {
  display: inline-block;
  background: #f1f5f9;
  color: #64748b;
  border-radius: 4px;
  padding: 1px 7px;
  font-size: 10.5px;
  font-weight: 500;
  letter-spacing: 0.02em;
}

/* 时间块 */
.tmpl-time-block {
  margin-top: 4px;
  background: #f8fafc;
  border-radius: 6px;
  padding: 6px 10px;
}

.tmpl-time-row {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 11px;
  color: #475569;
}

.tmpl-time-val { font-family: 'Fira Code', monospace; font-size: 10.5px; color: #334155; }
.tmpl-time-arrow { color: #94a3b8; }

.tmpl-time-empty {
  margin-top: 4px;
  font-size: 11px;
  color: #cbd5e1;
  font-style: italic;
  padding: 6px 10px;
  background: #fafafa;
  border-radius: 6px;
  border: 1px dashed #e8ecf2;
}

/* 操作按钮行 */
.tmpl-card-actions {
  padding: 9px 14px 13px;
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  border-top: 1px solid #f1f5f9;
}

.tmpl-action-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 5px 11px;
  font-size: 12px;
  font-weight: 600;
  border-radius: 6px;
  border: none;
  cursor: pointer;
  transition: all 0.15s;
}

.tmpl-action-btn--primary {
  background: #4f46e5;
  color: #fff;
}
.tmpl-action-btn--primary:hover { background: #4338ca; box-shadow: 0 2px 8px rgba(79,70,229,0.35); }

.tmpl-action-btn--ghost {
  background: #f1f5f9;
  color: #475569;
}
.tmpl-action-btn--ghost:hover { background: #e2e8f0; color: #334155; }

.tmpl-action-btn--warn {
  background: #fff7ed;
  color: #c2410c;
  border: 1px solid #fed7aa;
}
.tmpl-action-btn--warn:hover { background: #ffedd5; }

.tmpl-action-btn--danger {
  background: #fff5f5;
  color: #b91c1c;
  border: 1px solid #fecaca;
}
.tmpl-action-btn--danger:hover { background: #fee2e2; }

/* ── 上架对话框 ───────────────────────────────────────────────────── */
.tmpl-activate-dialog .el-dialog__header {
  padding: 18px 20px 14px;
  border-bottom: 1px solid #f1f5f9;
}
.tmpl-activate-dialog .el-dialog__title {
  font-size: 15px;
  font-weight: 700;
  color: #0f172a;
}
.tmpl-activate-dialog .el-dialog__body { padding: 0; }
.tmpl-activate-dialog .el-dialog__footer { padding: 0; }

.activate-dialog-body { display: flex; flex-direction: column; }

.activate-tmpl-preview {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px 20px;
  background: #f8fafc;
  border-bottom: 1px solid #f1f5f9;
}

.activate-tmpl-img {
  width: 80px;
  height: 52px;
  object-fit: cover;
  border-radius: 6px;
  border: 1px solid #e2e8f0;
}

.activate-tmpl-fallback {
  width: 80px; height: 52px;
  background: repeating-linear-gradient(45deg,#f1f5f9,#f1f5f9 6px,#e8ecf2 6px,#e8ecf2 12px);
  border-radius: 6px;
  display: flex; align-items: center; justify-content: center;
}

.activate-tmpl-name {
  font-size: 14px;
  font-weight: 650;
  color: #0f172a;
  line-height: 1.4;
}

.activate-tmpl-provider {
  font-size: 11px;
  color: #94a3b8;
  margin-top: 3px;
  font-family: 'Fira Code', monospace;
}

.activate-form {
  padding: 18px 20px 4px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.activate-field { display: flex; flex-direction: column; gap: 6px; }

.activate-label {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 12.5px;
  font-weight: 600;
  color: #475569;
}
.activate-label-sub {
  font-weight: 400;
  color: #94a3b8;
  margin-left: 4px;
}

.activate-notice {
  display: flex;
  align-items: flex-start;
  gap: 7px;
  font-size: 12px;
  color: #64748b;
  background: #f8fafc;
  border-radius: 0 0 6px 6px;
  padding: 12px 20px 16px;
  line-height: 1.6;
}

.activate-dialog-footer {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
  padding: 14px 20px;
  border-top: 1px solid #f1f5f9;
}

.activate-cancel-btn {
  padding: 7px 16px;
  font-size: 13px;
  font-weight: 500;
  color: #64748b;
  background: #f1f5f9;
  border: none;
  border-radius: 7px;
  cursor: pointer;
  transition: background 0.15s;
}
.activate-cancel-btn:hover { background: #e2e8f0; }

.activate-confirm-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 18px;
  font-size: 13px;
  font-weight: 600;
  color: #fff;
  background: #4f46e5;
  border: none;
  border-radius: 7px;
  cursor: pointer;
  transition: background 0.15s, box-shadow 0.15s;
}
.activate-confirm-btn:hover:not(:disabled) {
  background: #4338ca;
  box-shadow: 0 3px 10px rgba(79,70,229,0.35);
}
.activate-confirm-btn:disabled { opacity: 0.6; cursor: not-allowed; }

/* ── OfficePLUS 导入面板 ──────────────────────────────────────────── */
.op-import-panel {
  display: flex;
  flex-direction: column;
  gap: 18px;
}

/* 步骤引导栏 */
.op-steps-bar {
  display: flex;
  align-items: center;
  gap: 0;
  background: linear-gradient(125deg, #0f172a, #1e293b);
  border: 1px solid #334155;
  border-radius: 14px;
  padding: 18px 22px;
  flex-wrap: wrap;
  gap: 8px;
}

.op-step {
  display: flex;
  align-items: center;
  gap: 12px;
  flex: 1;
  min-width: 180px;
}

.op-step-num {
  width: 28px; height: 28px;
  border-radius: 50%;
  background: rgba(99, 102, 241, 0.25);
  border: 1.5px solid #6366f1;
  color: #a5b4fc;
  font-size: 13px;
  font-weight: 700;
  display: flex; align-items: center; justify-content: center;
  flex-shrink: 0;
  font-family: 'Fira Code', monospace;
}

.op-step-body { flex: 1; }

.op-step-title {
  font-size: 13px;
  font-weight: 600;
  color: #e2e8f0;
  line-height: 1.4;
}

.op-step-sub {
  font-size: 11px;
  color: #64748b;
  margin-top: 2px;
}

.op-step-link {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 5px 12px;
  font-size: 12px;
  font-weight: 600;
  color: #94a3b8;
  background: rgba(255,255,255,0.06);
  border: 1px solid #334155;
  border-radius: 7px;
  text-decoration: none;
  transition: all 0.15s;
  white-space: nowrap;
  flex-shrink: 0;
}
.op-step-link:hover { color: #e2e8f0; border-color: #4f46e5; background: rgba(79,70,229,0.15); }

.op-step-arrow {
  color: #334155;
  flex-shrink: 0;
  padding: 0 4px;
}

/* 拖拽上传区 */
@keyframes dropzone-pulse {
  0%, 100% { border-color: #6366f1; box-shadow: 0 0 0 0 rgba(99,102,241,0.2); }
  50%       { border-color: #818cf8; box-shadow: 0 0 0 6px rgba(99,102,241,0); }
}

.op-dropzone {
  border: 2px dashed #e2e8f0;
  border-radius: 16px;
  background: #fafbfc;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s, box-shadow 0.2s;
  min-height: 200px;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  overflow: hidden;
}
.op-dropzone:hover:not(.is-uploading) {
  border-color: #a5b4fc;
  background: #f5f3ff;
}
.op-dropzone.is-dragover {
  border-color: #6366f1;
  background: #eef2ff;
  animation: dropzone-pulse 1s ease-in-out infinite;
}
.op-dropzone.is-uploading {
  cursor: not-allowed;
  background: #f8fafc;
}
.op-dropzone.has-files {
  cursor: default;
  align-items: flex-start;
  justify-content: flex-start;
  min-height: auto;
}

/* 空态 */
.op-dz-idle {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 40px 20px;
  pointer-events: none;
  text-align: center;
}

.op-dz-icon {
  width: 64px; height: 64px;
  border-radius: 50%;
  background: #f1f5f9;
  display: flex; align-items: center; justify-content: center;
  color: #94a3b8;
  transition: background 0.2s, color 0.2s;
}
.op-dz-icon.is-dragover {
  background: #eef2ff;
  color: #6366f1;
}

.op-dz-main-text {
  font-size: 15px;
  font-weight: 600;
  color: #334155;
}

.op-dz-sub-text {
  font-size: 12.5px;
  color: #94a3b8;
  line-height: 1.5;
}

/* 上传中 */
.op-dz-uploading {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 14px;
  padding: 40px 20px;
  pointer-events: none;
  width: 100%;
}

.op-dz-spin-icon { color: #6366f1; }

.op-dz-uploading-text {
  font-size: 14px;
  font-weight: 600;
  color: #4f46e5;
}

.op-dz-progress-bar {
  width: 240px;
  height: 4px;
  background: #e2e8f0;
  border-radius: 99px;
  overflow: hidden;
}

.op-dz-progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #6366f1, #818cf8);
  border-radius: 99px;
  transition: width 0.3s ease;
}

/* 文件列表 */
.op-dz-files {
  width: 100%;
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
  gap: 0;
}

.op-dz-files-header {
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 12.5px;
  font-weight: 600;
  color: #334155;
  margin-bottom: 10px;
  padding-bottom: 10px;
  border-bottom: 1px solid #f1f5f9;
}

.op-dz-clear-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  margin-left: auto;
  padding: 3px 9px;
  font-size: 11px;
  font-weight: 600;
  color: #64748b;
  background: #f1f5f9;
  border: none;
  border-radius: 5px;
  cursor: pointer;
  transition: all 0.15s;
}
.op-dz-clear-btn:hover { background: #fee2e2; color: #b91c1c; }

.op-dz-file-list {
  display: flex;
  flex-direction: column;
  gap: 2px;
  max-height: 220px;
  overflow-y: auto;
}

.op-dz-file-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 8px;
  border-radius: 7px;
  transition: background 0.12s;
}
.op-dz-file-item:hover { background: #f8fafc; }

.op-dz-file-icon { color: #6366f1; flex-shrink: 0; }

.op-dz-file-name {
  flex: 1;
  font-size: 12.5px;
  color: #334155;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.op-dz-file-size {
  font-size: 11px;
  color: #94a3b8;
  font-family: 'Fira Code', monospace;
  flex-shrink: 0;
}

.op-dz-remove-btn {
  width: 20px; height: 20px;
  display: flex; align-items: center; justify-content: center;
  background: none;
  border: none;
  color: #cbd5e1;
  cursor: pointer;
  border-radius: 4px;
  flex-shrink: 0;
  transition: all 0.12s;
}
.op-dz-remove-btn:hover { background: #fee2e2; color: #b91c1c; }

.op-dz-add-more {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  margin-top: 10px;
  padding: 6px 10px;
  font-size: 12px;
  color: #6366f1;
  font-weight: 600;
  cursor: pointer;
  border-radius: 6px;
  transition: background 0.15s;
  align-self: flex-start;
}
.op-dz-add-more:hover { background: #eef2ff; }

/* 上传按钮行 */
.op-upload-action {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 14px 18px;
  background: #eef2ff;
  border: 1.5px solid #c7d2fe;
  border-radius: 12px;
}

.op-upload-summary {
  font-size: 13px;
  color: #4338ca;
  font-weight: 500;
}

.op-upload-submit-btn {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 9px 22px;
  font-size: 13.5px;
  font-weight: 700;
  color: #fff;
  background: #4f46e5;
  border: none;
  border-radius: 9px;
  cursor: pointer;
  transition: background 0.15s, box-shadow 0.15s, transform 0.1s;
}
.op-upload-submit-btn:hover {
  background: #4338ca;
  box-shadow: 0 4px 14px rgba(79,70,229,0.4);
  transform: translateY(-1px);
}
.op-upload-submit-btn:active { transform: translateY(0); }

/* 上传日志 */
.op-import-log {
  background: #0f172a;
  border: 1px solid #1e293b;
  border-radius: 12px;
  padding: 14px 16px;
  max-height: 240px;
  overflow-y: auto;
}

.op-log-header {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  font-weight: 600;
  color: #475569;
  text-transform: uppercase;
  letter-spacing: 0.07em;
  margin-bottom: 10px;
}

.op-log-clear-btn {
  margin-left: auto;
  padding: 2px 8px;
  font-size: 10.5px;
  color: #475569;
  background: rgba(71,85,105,0.2);
  border: none;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.15s;
}
.op-log-clear-btn:hover { background: rgba(239,68,68,0.2); color: #fca5a5; }

.op-log-item {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  padding: 5px 0;
  border-bottom: 1px solid #1e293b;
}
.op-log-item:last-child { border-bottom: none; }

.op-log-dot {
  width: 6px; height: 6px;
  border-radius: 50%;
  flex-shrink: 0;
  margin-top: 5px;
}
.op-log-item.ok   .op-log-dot { background: #22c55e; box-shadow: 0 0 5px rgba(34,197,94,0.5); }
.op-log-item.fail .op-log-dot { background: #ef4444; box-shadow: 0 0 5px rgba(239,68,68,0.5); }

.op-log-content {
  flex: 1;
  display: flex;
  align-items: baseline;
  gap: 8px;
  flex-wrap: wrap;
}

.op-log-text { font-size: 12px; line-height: 1.5; }
.op-log-item.ok   .op-log-text { color: #86efac; }
.op-log-item.fail .op-log-text { color: #fca5a5; }

.op-log-size {
  font-size: 10.5px;
  color: #475569;
  font-family: 'Fira Code', monospace;
  flex-shrink: 0;
}
</style>
