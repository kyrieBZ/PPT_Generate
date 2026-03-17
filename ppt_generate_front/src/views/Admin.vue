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
              <h3>洞察预备区</h3>
              <p>后续将接入大模型分析，为你输出用户偏好与模板推荐策略。</p>
              <ul>
                <li>用户高频主题识别</li>
                <li>模板转化率与留存</li>
                <li>失败原因聚类归因</li>
              </ul>
            </div>
            <div class="insight-card accent">
              <h3>今日提醒</h3>
              <p>当前成功率较上周上升 {{ successRateDelta }}，继续关注失败请求原因。</p>
              <div class="insight-tags">
                <span>生成质量</span>
                <span>模板覆盖</span>
                <span>用户活跃度</span>
              </div>
            </div>
          </div>
        </section>

        <section v-show="activeNav === 'charts'" class="dashboard-section" v-loading="metricsLoading">
          <div class="dashboard-header">
            <div>
              <h2>数据可视化看板</h2>
              <p>拖拽卡片调整顺序，切换图表类型。</p>
            </div>
            <div class="dashboard-controls">
              <el-select v-model="timeRange" size="large" class="control-select">
                <el-option label="最近 24 小时" value="day" />
                <el-option label="最近 7 天" value="week" />
                <el-option label="最近 30 天" value="month" />
              </el-select>
              <el-button class="ghost-btn" size="large" @click="resetDashboard">
                重置布局
              </el-button>
            </div>
          </div>

          <div class="dashboard-grid">
            <div
              v-for="card in chartCards"
              :key="card.id"
              class="dashboard-card"
              :class="{ dragging: dragState.draggingId === card.id }"
              draggable="true"
              @dragstart="onDragStart(card.id)"
              @dragend="onDragEnd"
              @dragover.prevent
              @drop="onDrop(card.id)"
            >
              <div class="card-header">
                <div class="card-title">
                  <span class="drag-handle">⋮⋮</span>
                  <div>
                    <h3>{{ card.title }}</h3>
                    <p>{{ card.subtitle }}</p>
                  </div>
                </div>
                <div class="card-actions">
                  <el-select
                    v-if="card.types.length > 1"
                    v-model="card.chartType"
                    size="small"
                    class="type-select"
                  >
                    <el-option
                      v-for="type in card.types"
                      :key="type"
                      :label="chartTypeLabels[type]"
                      :value="type"
                    />
                  </el-select>
                  <el-dropdown @command="(command) => exportCard(card.id, command)">
                    <el-button class="ghost-btn" size="small">
                      导出
                    </el-button>
                    <template #dropdown>
                      <el-dropdown-menu>
                        <el-dropdown-item command="png">导出 PNG</el-dropdown-item>
                        <el-dropdown-item command="pdf">导出 PDF</el-dropdown-item>
                      </el-dropdown-menu>
                    </template>
                  </el-dropdown>
                </div>
              </div>
              <div class="chart-wrap">
                <el-chart
                  :ref="(el) => setChartRef(card.id, el)"
                  :option="chartOptions[card.id]"
                  :height="card.height"
                />
              </div>
              <div class="card-footnote">
                {{ card.footnote }}
              </div>
            </div>
          </div>
        </section>

        <section v-show="activeNav === 'records'" class="records-section">
          <div class="records-header">
            <div>
              <h2>生成记录管理</h2>
              <p>展示全量生成记录，可按状态、关键字筛选。</p>
            </div>
            <div class="records-controls">
              <el-input
                v-model="searchQuery"
                size="large"
                placeholder="搜索标题/主题/用户"
                clearable
              />
              <el-select v-model="statusFilter" size="large" class="control-select">
                <el-option label="全部状态" value="all" />
                <el-option label="已完成" value="success" />
                <el-option label="失败" value="failed" />
              </el-select>
              <el-date-picker
                v-model="recordDateRange"
                type="daterange"
                range-separator="至"
                start-placeholder="开始日期"
                end-placeholder="结束日期"
                size="large"
              />
            </div>
          </div>

          <el-card class="records-table-card">
            <el-table
              :data="pagedHistory"
              stripe
              v-loading="adminHistoryLoading"
              element-loading-text="数据加载中..."
            >
              <el-table-column prop="id" label="ID" width="80" />
              <el-table-column prop="title" label="标题" min-width="180" />
              <el-table-column prop="userName" label="用户" width="140" />
              <el-table-column prop="templateName" label="模板" width="140" />
              <el-table-column label="状态" width="120">
                <template #default="{ row }">
                  <el-tag :type="statusTagType(row.status)">
                    {{ statusText(row.status) }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="创建时间" width="180">
                <template #default="{ row }">
                  {{ formatDate(row.createdAt) }}
                </template>
              </el-table-column>
              <el-table-column label="操作" width="120">
                <template #default>
                  <el-button size="small" class="ghost-btn">查看</el-button>
                </template>
              </el-table-column>
            </el-table>

            <div class="pagination-bar">
              <span>共 {{ filteredHistory.length }} 条记录</span>
              <el-pagination
                v-model:current-page="currentPage"
                :page-size="pageSize"
                layout="prev, pager, next"
                :total="filteredHistory.length"
                background
              />
            </div>
          </el-card>
        </section>

        <section v-show="activeNav === 'users'" class="records-section">
          <div class="records-header">
            <div>
              <h2>用户管理</h2>
              <p>管理员可禁用或启用用户账户。</p>
            </div>
            <div class="records-controls">
              <el-input
                v-model="userSearchQuery"
                size="large"
                placeholder="搜索用户名或邮箱"
                clearable
              />
              <el-select v-model="userStatusFilter" size="large" class="control-select">
                <el-option label="全部状态" value="all" />
                <el-option label="正常" value="active" />
                <el-option label="已禁用" value="disabled" />
              </el-select>
              <el-button class="ghost-btn" size="large" @click="loadUsers">
                刷新列表
              </el-button>
            </div>
          </div>

          <el-card class="records-table-card">
            <el-table
              :data="pagedUsers"
              stripe
              v-loading="usersLoading"
              element-loading-text="用户加载中..."
            >
              <el-table-column prop="id" label="ID" width="80" />
              <el-table-column prop="username" label="用户名" width="160" />
              <el-table-column prop="email" label="邮箱" min-width="200" />
              <el-table-column label="角色" width="120">
                <template #default="{ row }">
                  <el-tag :type="row.isAdmin ? 'warning' : 'info'">
                    {{ row.isAdmin ? '管理员' : '普通用户' }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="状态" width="120">
                <template #default="{ row }">
                  <el-tag :type="row.isDisabled ? 'danger' : 'success'">
                    {{ row.isDisabled ? '已禁用' : '正常' }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="最近登录" width="180">
                <template #default="{ row }">
                  {{ row.lastLogin ? dayjs(row.lastLogin).format('YYYY/MM/DD HH:mm') : '-' }}
                </template>
              </el-table-column>
              <el-table-column label="操作" width="140">
                <template #default="{ row }">
                  <el-button
                    size="small"
                    class="ghost-btn"
                    :disabled="row.id === currentUser?.id"
                    @click="toggleUserStatus(row)"
                  >
                    {{ row.isDisabled ? '启用' : '禁用' }}
                  </el-button>
                </template>
              </el-table-column>
            </el-table>

            <div class="pagination-bar">
              <span>共 {{ filteredUsers.length }} 位用户</span>
              <el-pagination
                v-model:current-page="userPage"
                :page-size="userPageSize"
                layout="prev, pager, next"
                :total="filteredUsers.length"
                background
              />
            </div>
          </el-card>
        </section>

        <!-- ── 偏好洞察 ──────────────────────────────────────────────────────── -->
        <section v-show="activeNav === 'insights'" class="records-section" v-loading="insightsLoading">
          <div class="records-header">
            <div>
              <h2>偏好洞察</h2>
              <p>基于全量 PPT 生成历史的用户行为与偏好分析。</p>
            </div>
            <el-button class="ghost-btn" size="large" @click="loadInsights">
              <el-icon><Refresh /></el-icon>
              刷新
            </el-button>
          </div>

          <div v-if="!insightsLoading && insightsData" class="insights-grid">

            <!-- 用户留存漏斗 -->
            <el-card class="insight-card insight-card--half" shadow="never">
              <template #header><span class="insight-card-title">用户转化漏斗</span></template>
              <ElChart :option="funnelChartOption" :height="280" />
            </el-card>

            <!-- 模型使用分布饼图 -->
            <el-card class="insight-card insight-card--half" shadow="never">
              <template #header><span class="insight-card-title">AI 模型使用分布</span></template>
              <ElChart :option="modelPieChartOption" :height="280" />
            </el-card>

            <!-- 页数分布柱状图 -->
            <el-card class="insight-card insight-card--half" shadow="never">
              <template #header><span class="insight-card-title">生成页数分布</span></template>
              <ElChart :option="pagesBarChartOption" :height="260" />
            </el-card>

            <!-- 热门主题标签云（用横向条形图模拟词云） -->
            <el-card class="insight-card insight-card--half" shadow="never">
              <template #header><span class="insight-card-title">热门生成主题（TOP 20）</span></template>
              <ElChart :option="topicsBarChartOption" :height="260" />
            </el-card>

            <!-- 生成高峰热力图（全宽） -->
            <el-card class="insight-card insight-card--full" shadow="never">
              <template #header><span class="insight-card-title">生成高峰时段热力图（24h × 周几）</span></template>
              <ElChart :option="heatmapChartOption" :height="220" />
            </el-card>

          </div>

          <div v-else-if="!insightsLoading" class="placeholder-card" style="margin-top:24px;">
            <p>暂无数据，请确保已有 PPT 生成记录。</p>
          </div>
        </section>

        <!-- ── 公告与通知管理 ─────────────────────────────────────────────── -->
        <section v-show="activeNav === 'announcements'" class="records-section">
          <div class="records-header">
            <div>
              <h2>公告与通知管理</h2>
              <p>创建、编辑站内公告，用户端登录后将在顶部看到有效公告横幅。</p>
            </div>
            <div class="records-controls">
              <el-button type="primary" size="large" @click="openAnnouncementDialog()">
                <el-icon><Plus /></el-icon>
                发布公告
              </el-button>
            </div>
          </div>

          <el-table
            :data="announcements"
            v-loading="announcementsLoading"
            style="width: 100%; margin-top: 16px;"
            row-key="id"
            stripe
          >
            <el-table-column label="标题" prop="title" min-width="180" show-overflow-tooltip />
            <el-table-column label="内容" prop="content" min-width="240" show-overflow-tooltip />
            <el-table-column label="置顶" width="70" align="center">
              <template #default="{ row }">
                <el-tag :type="row.is_pinned ? 'warning' : 'info'" size="small">
                  {{ row.is_pinned ? '是' : '否' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column label="生效时间" width="160">
              <template #default="{ row }">{{ formatDate(row.starts_at) }}</template>
            </el-table-column>
            <el-table-column label="过期时间" width="160">
              <template #default="{ row }">
                {{ row.expires_at ? formatDate(row.expires_at) : '永不过期' }}
              </template>
            </el-table-column>
            <el-table-column label="状态" width="90" align="center">
              <template #default="{ row }">
                <el-tag :type="announcementStatusType(row)" size="small">
                  {{ announcementStatusText(row) }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column label="操作" width="140" fixed="right" align="center">
              <template #default="{ row }">
                <el-button type="primary" link size="small" @click="openAnnouncementDialog(row)">
                  编辑
                </el-button>
                <el-button type="danger" link size="small" @click="deleteAnnouncement(row)">
                  删除
                </el-button>
              </template>
            </el-table-column>
          </el-table>

          <div class="pagination-bar" v-if="announcementTotal > announcementPageSize">
            <el-pagination
              v-model:current-page="announcementPage"
              :page-size="announcementPageSize"
              layout="prev, pager, next, total"
              :total="announcementTotal"
              background
              @current-change="loadAnnouncements"
            />
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

        <!-- ── 素材全局管理 ───────────────────────────────────────────────── -->
        <section v-show="activeNav === 'materials'" class="records-section">
          <div class="records-header">
            <div>
              <h2>素材全局管理</h2>
              <p>查看并管理所有用户上传的素材文件。</p>
            </div>
            <div class="records-controls">
              <el-input
                v-model="materialSearch.userId"
                size="large"
                placeholder="用户 ID"
                clearable
                style="width: 130px"
              />
              <el-select v-model="materialSearch.status" size="large" class="control-select" style="width: 140px">
                <el-option label="全部状态" value="" />
                <el-option label="已完成" value="completed" />
                <el-option label="处理中" value="pending" />
                <el-option label="提取中" value="extracting" />
                <el-option label="失败" value="failed" />
              </el-select>
              <el-select v-model="materialSearch.fileType" size="large" class="control-select" style="width: 120px">
                <el-option label="全部类型" value="" />
                <el-option label="PDF" value="pdf" />
                <el-option label="DOCX" value="docx" />
                <el-option label="TXT" value="txt" />
              </el-select>
              <el-button type="primary" size="large" @click="loadMaterials(1)">
                <el-icon><Search /></el-icon>
                搜索
              </el-button>
              <el-button
                type="danger"
                size="large"
                :disabled="materialSelection.length === 0"
                @click="batchDeleteMaterials"
              >
                批量删除 ({{ materialSelection.length }})
              </el-button>
            </div>
          </div>

          <!-- 统计摘要卡片 -->
          <div class="material-stats-row" v-if="materialStats.total >= 0">
            <div class="mstat-card">
              <span class="mstat-label">总素材数</span>
              <span class="mstat-value">{{ materialStats.total }}</span>
            </div>
            <div class="mstat-card">
              <span class="mstat-label">总占用空间</span>
              <span class="mstat-value">{{ formatBytes(materialStats.totalSize) }}</span>
            </div>
            <div class="mstat-card success">
              <span class="mstat-label">已完成提取</span>
              <span class="mstat-value">{{ materialStats.completed }}</span>
            </div>
            <div class="mstat-card warning">
              <span class="mstat-label">处理中</span>
              <span class="mstat-value">{{ materialStats.pending }}</span>
            </div>
            <div class="mstat-card danger">
              <span class="mstat-label">失败</span>
              <span class="mstat-value">{{ materialStats.failed }}</span>
            </div>
          </div>

          <el-card class="records-table-card">
            <el-table
              :data="materials"
              stripe
              v-loading="materialsLoading"
              element-loading-text="加载中..."
              @selection-change="(rows) => (materialSelection = rows)"
            >
              <el-table-column type="selection" width="50" />
              <el-table-column prop="id" label="ID" width="240" show-overflow-tooltip />
              <el-table-column prop="username" label="用户" width="130" show-overflow-tooltip />
              <el-table-column prop="filename" label="文件名" min-width="180" show-overflow-tooltip />
              <el-table-column label="类型" width="80">
                <template #default="{ row }">
                  <el-tag size="small" type="info">{{ row.fileType?.toUpperCase() || '-' }}</el-tag>
                </template>
              </el-table-column>
              <el-table-column label="大小" width="100">
                <template #default="{ row }">{{ formatBytes(row.fileSize) }}</template>
              </el-table-column>
              <el-table-column label="状态" width="110">
                <template #default="{ row }">
                  <el-tag :type="materialStatusType(row.status)" size="small">
                    {{ materialStatusText(row.status) }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="上传时间" width="175">
                <template #default="{ row }">
                  {{ row.createdAt ? dayjs.unix(row.createdAt).format('YYYY/MM/DD HH:mm') : '-' }}
                </template>
              </el-table-column>
              <el-table-column label="操作" width="210" fixed="right">
                <template #default="{ row }">
                  <el-button
                    size="small"
                    type="primary"
                    plain
                    @click="openMaterialDrawer(row, 'preview')"
                  >预览</el-button>
                  <el-button
                    size="small"
                    type="warning"
                    plain
                    @click="openMaterialDrawer(row, 'review')"
                  >审核</el-button>
                  <el-button
                    size="small"
                    type="danger"
                    plain
                    @click="deleteMaterial(row)"
                  >删除</el-button>
                </template>
              </el-table-column>
            </el-table>

            <div class="pagination-bar">
              <span>共 {{ materialTotal }} 条记录</span>
              <el-pagination
                v-model:current-page="materialPage"
                :page-size="materialPageSize"
                layout="prev, pager, next"
                :total="materialTotal"
                background
                @current-change="loadMaterials"
              />
            </div>
          </el-card>
        </section>

        <section v-show="activeNav === 'settings'" class="placeholder-section">
          <div class="placeholder-card">
            <h2>系统设置模块</h2>
            <p>后续可在此配置权限、模板、通知与告警。</p>
          </div>
        </section>
      </div>
    </section>

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
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue'
import { useStore } from 'vuex'
import { useRouter } from 'vue-router'
import dayjs from 'dayjs'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  ArrowLeft,
  ArrowRight,
  DataLine,
  Download,
  Document,
  Histogram,
  PieChart,
  Plus,
  Refresh,
  Search,
  Setting,
  TrendCharts,
  UserFilled,
  FolderOpened,
  Bell
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
  { id: 'charts', label: '数据看板', icon: TrendCharts },
  { id: 'records', label: '生成记录', icon: Document, badge: 'NEW' },
  { id: 'users', label: '用户管理', icon: UserFilled },
  { id: 'materials', label: '素材管理', icon: FolderOpened },
  { id: 'announcements', label: '公告管理', icon: Bell },
  { id: 'insights', label: '偏好洞察', icon: PieChart },
  { id: 'settings', label: '系统设置', icon: Setting, disabled: true }
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
  return ['failed', 'error', 'rejected', 'canceled'].includes(value)
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

const statCards = computed(() => [
  {
    id: 'total',
    label: '累计生成',
    value: totalCount.value || 0,
    note: '包含所有历史记录',
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
    note: '基于生成记录去重',
    icon: UserFilled
  },
  {
    id: 'templates',
    label: '模板覆盖',
    value: templateCount.value || 0,
    note: '近期热门模板数量',
    icon: TrendCharts
  }
])

const successRateDelta = computed(() => {
  const delta = Math.max(2, Math.min(12, Math.round(successRate.value / 12)))
  return `+${delta}%`
})

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
    items = items.filter((item) =>
      statusFilter.value === 'failed' ? isFailureStatus(item.status) : !isFailureStatus(item.status)
    )
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
      {
        confirmButtonText: '确认',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    await adminAPI.updateUserStatus({ userId: user.id, disabled: nextDisabled })
    user.isDisabled = nextDisabled
    ElMessage.success(nextDisabled ? '用户已禁用' : '用户已启用')
  } catch (error) {
    if (error !== 'cancel' && error !== 'close') {
      // 拦截器已统一 ElMessage，此处仅记录
      console.error('更新用户状态失败:', error?.userMessage || error?.response?.data?.message)
    }
  }
}

const refreshData = () => {
  // 根据当前激活的 tab 刷新对应模块数据
  switch (activeNav.value) {
    case 'overview':
      loadMetrics()
      break
    case 'charts':
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
      break
    default:
      store.dispatch('fetchAdminHistory').catch(() => {})
      loadMetrics()
  }
}

const statusText = (status) => (isFailureStatus(status) ? '失败' : '已完成')
const statusTagType = (status) => (isFailureStatus(status) ? 'danger' : 'success')
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

// 用户漏斗
const funnelChartOption = computed(() => {
  const d = insightsData.value
  const reg  = d?.userFunnel?.registered     || 0
  const gen1 = d?.userFunnel?.generatedOnce  || 0
  const genM = d?.userFunnel?.generatedMulti || 0
  return {
    tooltip: { trigger: 'item', formatter: '{b}: {c} 人' },
    series: [{
      type: 'funnel',
      left: '10%', width: '80%',
      label: { show: true, position: 'inside', formatter: '{b}\n{c}人' },
      data: [
        { value: reg,  name: '注册用户' },
        { value: gen1, name: '生成过 PPT' },
        { value: genM, name: '高频用户(≥3次)' }
      ],
      color: ['#409eff', '#67c23a', '#e6a23c']
    }]
  }
})

// 模型饼图
const modelPieChartOption = computed(() => {
  const items = (insightsData.value?.modelUsage || []).map((m) => ({
    name: m.model,
    value: m.count
  }))
  return {
    tooltip: { trigger: 'item', formatter: '{b}: {c} 次 ({d}%)' },
    legend: { orient: 'vertical', right: 10, top: 'center', type: 'scroll' },
    series: [{
      type: 'pie',
      radius: ['40%', '70%'],
      center: ['40%', '50%'],
      data: items,
      label: { show: false },
      emphasis: { label: { show: true, fontSize: 14, fontWeight: 'bold' } }
    }]
  }
})

// 页数分布柱状图
const pagesBarChartOption = computed(() => {
  const items = insightsData.value?.pagesDistribution || []
  return {
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'category', data: items.map((i) => i.label) },
    yAxis: { type: 'value', name: '次数' },
    series: [{
      type: 'bar',
      data: items.map((i) => i.value),
      itemStyle: { color: '#409eff', borderRadius: [4, 4, 0, 0] },
      label: { show: true, position: 'top', color: '#606266' }
    }],
    grid: { left: 40, right: 20, top: 30, bottom: 30 }
  }
})

// 热门主题横向条形图
const topicsBarChartOption = computed(() => {
  const items = [...(insightsData.value?.topTopics || [])].reverse()
  return {
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    grid: { left: 140, right: 20, top: 10, bottom: 20 },
    xAxis: { type: 'value' },
    yAxis: {
      type: 'category',
      data: items.map((t) => t.keyword),
      axisLabel: { width: 120, overflow: 'truncate', fontSize: 12 }
    },
    series: [{
      type: 'bar',
      data: items.map((t) => t.count),
      itemStyle: { color: '#67c23a', borderRadius: [0, 4, 4, 0] },
      label: { show: true, position: 'right', color: '#606266' }
    }]
  }
})

// 热力图 (24h × 7day)
const WEEKDAY_LABELS = ['周一', '周二', '周三', '周四', '周五', '周六', '周日']
const HOUR_LABELS = Array.from({ length: 24 }, (_, i) => `${String(i).padStart(2, '0')}:00`)

const heatmapChartOption = computed(() => {
  const cells = insightsData.value?.hourlyHeatmap || []
  const maxVal = cells.reduce((m, c) => Math.max(m, c.count), 0) || 1
  const data = cells.map((c) => [c.hour, c.weekday, c.count])
  return {
    tooltip: {
      formatter: (p) => `${WEEKDAY_LABELS[p.data[1]]} ${HOUR_LABELS[p.data[0]]}: ${p.data[2]} 次`
    },
    grid: { left: 50, right: 20, top: 20, bottom: 40 },
    xAxis: {
      type: 'category',
      data: HOUR_LABELS,
      axisLabel: { fontSize: 10, interval: 1, rotate: 45 },
      splitArea: { show: true }
    },
    yAxis: {
      type: 'category',
      data: WEEKDAY_LABELS,
      splitArea: { show: true }
    },
    visualMap: {
      min: 0, max: maxVal,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: 0,
      inRange: { color: ['#eef7ff', '#409eff', '#0050b3'] }
    },
    series: [{
      type: 'heatmap',
      data,
      label: { show: false },
      emphasis: { itemStyle: { shadowBlur: 10, shadowColor: 'rgba(0,0,0,0.5)' } }
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
  if (value === 'charts') {
    resizeCharts()
  }
  if (value === 'materials' && materials.value.length === 0 && !materialsLoading.value) {
    loadMaterials(1)
    loadMaterialStats()
  }
  if (value === 'announcements' && announcements.value.length === 0 && !announcementsLoading.value) {
    loadAnnouncements(1)
  }
  if (value === 'insights' && !insightsData.value && !insightsLoading.value) {
    loadInsights()
  }
})

onMounted(() => {
  if (currentUser.value?.isAdmin) {
    store.dispatch('fetchAdminHistory').catch(() => {})
    loadMetrics()
    loadUsers()
  }
  if (activeNav.value === 'charts') {
    resizeCharts()
  }
})
</script>

<style scoped>
@import url('https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@400;500;600;700&display=swap');

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
.dashboard-section,
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

/* ── 偏好洞察布局 ──────────────────────────────────────────────────────────── */
.insights-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
  margin-top: 16px;
}
.insight-card--half {
  grid-column: span 1;
}
.insight-card--full {
  grid-column: span 2;
}
.insight-card-title {
  font-size: 14px;
  font-weight: 600;
  color: #303133;
}

/* 旧的 dark insight-card（数据看板用） */
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

.dashboard-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
}

.dashboard-header p {
  color: #64748b;
}

.dashboard-controls {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.dashboard-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(360px, 1fr));
  gap: 20px;
}

.dashboard-card {
  background: var(--color-bg-card);
  border-radius: var(--radius-card);
  padding: var(--space-lg);
  border: 1px solid rgba(0, 0, 0, 0.06);
  box-shadow: var(--shadow-card);
  display: grid;
  grid-template-rows: auto 1fr auto;
  gap: var(--space-md);
  min-height: 380px;
  transition: box-shadow var(--transition-default), transform var(--transition-default);
}

.dashboard-card:hover {
  box-shadow: var(--shadow-card-hover);
}

.dashboard-card.dragging {
  opacity: 0.6;
  transform: scale(0.98);
}

.card-header {
  display: flex;
  justify-content: space-between;
  gap: 12px;
}

.card-title {
  display: flex;
  gap: 10px;
}

.card-title h3 {
  margin-bottom: 4px;
}

.card-title p {
  color: #64748b;
  font-size: 0.85rem;
}

.drag-handle {
  color: #94a3b8;
  font-size: 1.2rem;
  padding-top: 2px;
}

.card-actions {
  display: flex;
  gap: 8px;
  align-items: center;
}

.chart-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 260px;
}

.chart-wrap .el-chart {
  width: 100%;
  height: 100%;
}

.type-select {
  min-width: 100px;
}

.card-footnote {
  font-size: 0.75rem;
  color: #94a3b8;
  padding-left: 6px;
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

  .dashboard-header {
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
</style>
