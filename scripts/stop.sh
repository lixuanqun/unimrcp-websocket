#!/bin/bash
#
# UniMRCP Server 停止脚本
#
# 用法: ./stop.sh [options]
#   -f, --force     强制停止（使用 SIGKILL）
#   -r, --root-dir  指定根目录
#   -h, --help      显示帮助信息
#

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 默认配置
PID_FILE="${ROOT_DIR}/var/unimrcpserver.pid"

# 默认选项
FORCE=0
CUSTOM_ROOT=""

# 停止超时时间（秒）
STOP_TIMEOUT=30

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示帮助
show_help() {
    echo "UniMRCP Server 停止脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -f, --force         强制停止（使用 SIGKILL）"
    echo "  -r, --root-dir DIR  指定 UniMRCP 根目录"
    echo "  -h, --help          显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                  # 优雅停止"
    echo "  $0 -f               # 强制停止"
    echo "  $0 -r /opt/unimrcp  # 指定根目录"
    echo ""
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -f|--force)
                FORCE=1
                shift
                ;;
            -r|--root-dir)
                CUSTOM_ROOT="$2"
                shift 2
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# 更新路径配置
update_paths() {
    if [ -n "$CUSTOM_ROOT" ]; then
        ROOT_DIR="$CUSTOM_ROOT"
        PID_FILE="${ROOT_DIR}/var/unimrcpserver.pid"
    fi
}

# 获取服务器 PID
get_pid() {
    local pid=""
    
    # 首先从 PID 文件获取
    if [ -f "$PID_FILE" ]; then
        pid=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
            echo "$pid"
            return 0
        fi
    fi
    
    # 如果 PID 文件不存在或进程不存在，尝试通过进程名查找
    pid=$(pgrep -f "unimrcpserver" 2>/dev/null | head -1)
    if [ -n "$pid" ]; then
        echo "$pid"
        return 0
    fi
    
    return 1
}

# 检查进程是否存在
is_running() {
    local pid="$1"
    if [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
        return 0
    fi
    return 1
}

# 等待进程停止
wait_for_stop() {
    local pid="$1"
    local timeout="$2"
    local count=0
    
    while [ $count -lt $timeout ]; do
        if ! is_running "$pid"; then
            return 0
        fi
        sleep 1
        count=$((count + 1))
        echo -n "."
    done
    echo ""
    return 1
}

# 停止服务器
stop_server() {
    print_info "正在停止 UniMRCP Server..."

    # 获取 PID
    local pid=$(get_pid)
    
    if [ -z "$pid" ]; then
        print_warn "UniMRCP Server 未在运行"
        # 清理可能存在的 PID 文件
        rm -f "$PID_FILE" 2>/dev/null || true
        exit 0
    fi

    print_info "找到 UniMRCP Server 进程 (PID: $pid)"

    if [ $FORCE -eq 1 ]; then
        # 强制停止
        print_warn "强制停止进程..."
        kill -9 "$pid" 2>/dev/null || true
    else
        # 优雅停止
        print_info "发送 SIGTERM 信号..."
        kill -15 "$pid" 2>/dev/null || true
        
        echo -n "等待进程退出 "
        if ! wait_for_stop "$pid" "$STOP_TIMEOUT"; then
            print_warn "进程未在 ${STOP_TIMEOUT} 秒内退出，发送 SIGKILL..."
            kill -9 "$pid" 2>/dev/null || true
            sleep 1
        fi
    fi

    # 验证进程已停止
    if is_running "$pid"; then
        print_error "无法停止进程 (PID: $pid)"
        exit 1
    fi

    # 清理 PID 文件
    rm -f "$PID_FILE" 2>/dev/null || true

    print_info "UniMRCP Server 已停止"
}

# 主函数
main() {
    parse_args "$@"
    update_paths
    stop_server
}

main "$@"
