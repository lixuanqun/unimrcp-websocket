#!/bin/bash
#
# UniMRCP Server 状态检查脚本
#
# 用法: ./status.sh [options]
#   -r, --root-dir  指定根目录
#   -v, --verbose   显示详细信息
#   -h, --help      显示帮助信息
#

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 默认配置
PID_FILE="${ROOT_DIR}/var/unimrcpserver.pid"
LOG_DIR="${ROOT_DIR}/log"

# 默认选项
VERBOSE=0
CUSTOM_ROOT=""

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

print_status() {
    echo -e "${BLUE}$1${NC}"
}

# 显示帮助
show_help() {
    echo "UniMRCP Server 状态检查脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -r, --root-dir DIR  指定 UniMRCP 根目录"
    echo "  -v, --verbose       显示详细信息"
    echo "  -h, --help          显示此帮助信息"
    echo ""
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -v|--verbose)
                VERBOSE=1
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
        LOG_DIR="${ROOT_DIR}/log"
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

# 显示状态
show_status() {
    echo ""
    echo "=========================================="
    echo "     UniMRCP Server 状态"
    echo "=========================================="
    echo ""

    local pid=$(get_pid)
    
    if [ -n "$pid" ]; then
        echo -e "状态:       ${GREEN}运行中${NC}"
        echo "PID:        $pid"
        
        # 显示进程信息
        if [ $VERBOSE -eq 1 ]; then
            echo ""
            echo "进程详情:"
            ps -p "$pid" -o pid,ppid,user,%cpu,%mem,etime,command --no-headers 2>/dev/null | head -1
            
            # 显示内存使用
            echo ""
            echo "内存使用:"
            ps -p "$pid" -o rss,vsz --no-headers 2>/dev/null | awk '{printf "  RSS: %.2f MB, VSZ: %.2f MB\n", $1/1024, $2/1024}'
            
            # 显示打开的文件数
            local fd_count=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l)
            echo "打开文件数: $fd_count"
            
            # 显示网络连接
            echo ""
            echo "网络连接:"
            netstat -tlnp 2>/dev/null | grep "$pid" | head -10 || ss -tlnp 2>/dev/null | grep "$pid" | head -10 || echo "  (无法获取网络连接信息)"
        fi
    else
        echo -e "状态:       ${RED}未运行${NC}"
    fi
    
    echo ""
    echo "配置信息:"
    echo "  根目录:   $ROOT_DIR"
    echo "  PID 文件: $PID_FILE"
    echo "  日志目录: $LOG_DIR"
    
    if [ $VERBOSE -eq 1 ]; then
        # 显示最近的日志
        echo ""
        echo "最近日志 (最后 10 行):"
        local latest_log=$(ls -t "${LOG_DIR}"/unimrcpserver*.log 2>/dev/null | head -1)
        if [ -n "$latest_log" ] && [ -f "$latest_log" ]; then
            tail -10 "$latest_log" | sed 's/^/  /'
        else
            echo "  (无日志文件)"
        fi
    fi
    
    echo ""
    echo "=========================================="
    echo ""
    
    # 返回状态码
    if [ -n "$pid" ]; then
        return 0
    else
        return 1
    fi
}

# 主函数
main() {
    parse_args "$@"
    update_paths
    show_status
}

main "$@"
