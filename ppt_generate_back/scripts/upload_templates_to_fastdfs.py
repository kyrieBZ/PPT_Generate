#!/usr/bin/env python3
"""
upload_templates_to_fastdfs.py
批量将本地模板文件（.pptx）和缩略图（.png/.jpg）上传到 FastDFS，
并将映射关系写入 MySQL template_fastdfs_map 表。

用法：
  python3 scripts/upload_templates_to_fastdfs.py \
      --config config/config.json \
      [--templates-dir assets/templates] \
      [--thumbnails-dir assets/template_thumbnails] \
      [--dry-run]

依赖：
  pip install requests mysql-connector-python

注意：
  - 脚本从 config.json 读取 fastdfs 和 database 配置。
  - FastDFS HTTP 上传需要服务器已部署 fdfs_httpd 或 fastdfs-nginx-module。
  - 已上传（数据库中已有记录）的模板默认跳过，使用 --force 强制重新上传。
"""

import argparse
import json
import os
import subprocess
import sys
import time

try:
    import mysql.connector
except ImportError:
    print("缺少 mysql-connector-python 库，请执行: pip install mysql-connector-python", file=sys.stderr)
    sys.exit(1)


def load_config(config_path: str) -> dict:
    with open(config_path, encoding="utf-8") as f:
        return json.load(f)


def get_db_conn(db_cfg: dict):
    return mysql.connector.connect(
        host=db_cfg.get("host", "127.0.0.1"),
        port=int(db_cfg.get("port", 3306)),
        user=db_cfg["user"],
        password=db_cfg["password"],
        database=db_cfg["name"],
        charset="utf8mb4",
    )


def get_existing_entries(cursor) -> set:
    """返回数据库中已有 pptx 记录的 template_id 集合。"""
    try:
        cursor.execute(
            "SELECT template_id FROM template_fastdfs_map WHERE pptx_file_id IS NOT NULL AND pptx_file_id != ''"
        )
        return {row[0] for row in cursor.fetchall()}
    except mysql.connector.Error:
        return set()


def upload_to_fastdfs(tracker_url: str, group_name: str, local_path: str,
                       timeout: int = 60, client_conf: str = "/etc/fdfs/client.conf") -> str | None:
    """
    通过 fdfs_upload_file 命令行工具上传文件到 FastDFS。
    新版 FastDFS（v6.x）已移除 Tracker HTTP 上传接口，必须使用 TCP 客户端上传。
    返回 file_id（如 group1/M00/00/00/xxx.pptx），失败返回 None。
    """
    if not os.path.exists(local_path):
        print(f"  [SKIP] 文件不存在: {local_path}")
        return None

    if not os.path.exists(client_conf):
        print(f"  [ERROR] 找不到 FastDFS 客户端配置: {client_conf}")
        return None

    try:
        result = subprocess.run(
            ["fdfs_upload_file", client_conf, local_path],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except FileNotFoundError:
        print("  [ERROR] 找不到 fdfs_upload_file 命令，请确认 FastDFS 已正确安装")
        return None
    except subprocess.TimeoutExpired:
        print(f"  [ERROR] 上传超时（>{timeout}s）")
        return None

    if result.returncode != 0:
        err = (result.stderr or result.stdout).strip()
        print(f"  [ERROR] fdfs_upload_file 失败: {err}")
        return None

    file_id = result.stdout.strip()
    if not file_id.startswith("group"):
        print(f"  [WARN] 无法解析 file_id，原始输出: {file_id[:200]}")
        return None

    return file_id


def build_access_url(storage_http_url: str, file_id: str) -> str:
    base = storage_http_url.rstrip("/")
    return f"{base}/{file_id}"


def upsert_entry(cursor, conn, template_id: str, pptx_fid: str, pptx_url: str,
                  thumb_fid: str, thumb_url: str) -> bool:
    sql = """
        INSERT INTO template_fastdfs_map
          (template_id, pptx_file_id, pptx_url, thumbnail_file_id, thumbnail_url, uploaded_at)
        VALUES (%s, %s, %s, %s, %s, NOW())
        ON DUPLICATE KEY UPDATE
          pptx_file_id      = IF(%s != '', %s, pptx_file_id),
          pptx_url          = IF(%s != '', %s, pptx_url),
          thumbnail_file_id = IF(%s != '', %s, thumbnail_file_id),
          thumbnail_url     = IF(%s != '', %s, thumbnail_url),
          updated_at        = NOW()
    """
    params = (
        template_id, pptx_fid, pptx_url, thumb_fid, thumb_url,
        # ON DUPLICATE UPDATE params
        pptx_fid, pptx_fid,
        pptx_url, pptx_url,
        thumb_fid, thumb_fid,
        thumb_url, thumb_url,
    )
    try:
        cursor.execute(sql, params)
        conn.commit()
        return True
    except mysql.connector.Error as e:
        print(f"  [ERROR] 写入数据库失败: {e}")
        conn.rollback()
        return False


def get_template_ids(templates_dir: str) -> list[str]:
    """从 templates 目录中获取所有模板 ID（文件名去掉 .pptx）。"""
    ids = []
    if not os.path.isdir(templates_dir):
        return ids
    for name in sorted(os.listdir(templates_dir)):
        if name.lower().endswith(".pptx"):
            ids.append(os.path.splitext(name)[0])
    return ids


def find_thumbnail(thumbnails_dir: str, template_id: str) -> str | None:
    for ext in (".png", ".jpg", ".jpeg"):
        path = os.path.join(thumbnails_dir, template_id + ext)
        if os.path.exists(path):
            return path
    return None


def main():
    parser = argparse.ArgumentParser(description="批量上传模板文件到 FastDFS")
    parser.add_argument("--config", default="config/config.json", help="配置文件路径")
    parser.add_argument("--templates-dir", default="assets/templates", help="模板 .pptx 目录")
    parser.add_argument("--thumbnails-dir", default="assets/template_thumbnails", help="缩略图目录")
    parser.add_argument("--dry-run", action="store_true", help="仅打印计划，不实际上传")
    parser.add_argument("--force", action="store_true", help="强制重新上传已有记录的模板")
    parser.add_argument("--template-id", help="仅迁移指定 template_id，不指定则迁移全部")
    args = parser.parse_args()

    # 加载配置
    cfg = load_config(args.config)
    fdfs_cfg = cfg.get("fastdfs", {})
    db_cfg = cfg.get("database", {})

    if not fdfs_cfg.get("enabled", False):
        print("[WARN] config.json 中 fastdfs.enabled = false，请先启用后重试。")
        sys.exit(1)

    tracker_url    = fdfs_cfg.get("tracker_http_url", "http://127.0.0.1:8080")
    storage_url    = fdfs_cfg.get("storage_http_url", "http://127.0.0.1:8888")
    group_name     = fdfs_cfg.get("group_name", "group1")
    upload_timeout = int(fdfs_cfg.get("upload_timeout_seconds", 60))
    client_conf    = fdfs_cfg.get("client_conf", "/etc/fdfs/client.conf")

    print(f"FastDFS Tracker: {tracker_url}")
    print(f"FastDFS Storage: {storage_url}")
    print(f"FastDFS client.conf: {client_conf}")
    print(f"Group: {group_name}")

    # 收集模板 ID
    if args.template_id:
        template_ids = [args.template_id]
    else:
        template_ids = get_template_ids(args.templates_dir)

    if not template_ids:
        print(f"[INFO] 在 {args.templates_dir} 中未找到任何 .pptx 文件。")
        sys.exit(0)

    print(f"\n共发现 {len(template_ids)} 个模板：{template_ids[:5]}{'...' if len(template_ids) > 5 else ''}")

    if args.dry_run:
        print("\n[DRY RUN] 以下为计划上传的文件：")
        for tid in template_ids:
            pptx  = os.path.join(args.templates_dir, tid + ".pptx")
            thumb = find_thumbnail(args.thumbnails_dir, tid)
            print(f"  模板: {tid}")
            print(f"    pptx: {pptx} ({'存在' if os.path.exists(pptx) else '不存在'})")
            print(f"    缩略图: {thumb or '无'}")
        print("\n[DRY RUN] 未实际上传。")
        sys.exit(0)

    # 连接数据库
    try:
        conn = get_db_conn(db_cfg)
        cursor = conn.cursor()
    except mysql.connector.Error as e:
        print(f"[ERROR] 数据库连接失败: {e}", file=sys.stderr)
        sys.exit(1)

    existing = get_existing_entries(cursor) if not args.force else set()

    success_count = 0
    skip_count = 0
    fail_count = 0

    for tid in template_ids:
        print(f"\n>>> 处理模板: {tid}")

        if tid in existing and not args.force:
            print(f"  [SKIP] 已有 FastDFS 记录，使用 --force 强制重新上传")
            skip_count += 1
            continue

        pptx_path  = os.path.join(args.templates_dir, tid + ".pptx")
        thumb_path = find_thumbnail(args.thumbnails_dir, tid)

        pptx_fid  = ""
        pptx_url  = ""
        thumb_fid = ""
        thumb_url = ""

        # 上传 pptx
        if os.path.exists(pptx_path):
            size_mb = os.path.getsize(pptx_path) / 1024 / 1024
            print(f"  上传 pptx ({size_mb:.1f} MB): {pptx_path}")
            fid = upload_to_fastdfs(tracker_url, group_name, pptx_path, upload_timeout, client_conf)
            if fid:
                pptx_fid = fid
                pptx_url = build_access_url(storage_url, fid)
                print(f"  [OK] pptx -> {fid}")
            else:
                print(f"  [FAIL] pptx 上传失败")
                fail_count += 1
                continue
        else:
            print(f"  [SKIP] pptx 文件不存在: {pptx_path}")

        # 上传缩略图
        if thumb_path:
            print(f"  上传缩略图: {thumb_path}")
            fid = upload_to_fastdfs(tracker_url, group_name, thumb_path, upload_timeout, client_conf)
            if fid:
                thumb_fid = fid
                thumb_url = build_access_url(storage_url, fid)
                print(f"  [OK] 缩略图 -> {fid}")
            else:
                print(f"  [WARN] 缩略图上传失败，继续写入 pptx 记录")
        else:
            print(f"  [INFO] 无缩略图文件")

        # 写入数据库
        if pptx_fid or thumb_fid:
            ok = upsert_entry(cursor, conn, tid, pptx_fid, pptx_url, thumb_fid, thumb_url)
            if ok:
                print(f"  [DB] 已写入 template_fastdfs_map")
                success_count += 1
            else:
                fail_count += 1
        else:
            print(f"  [SKIP] 无文件上传成功，跳过数据库写入")
            skip_count += 1

        time.sleep(0.1)  # 避免对 FastDFS 过快并发

    cursor.close()
    conn.close()

    print(f"\n{'=' * 50}")
    print(f"迁移完成：成功 {success_count}，跳过 {skip_count}，失败 {fail_count}")
    if fail_count > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
