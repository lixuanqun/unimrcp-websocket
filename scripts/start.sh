#!/bin/bash
#
# UniMRCP Server 启动脚本
#
# 用法: ./start.sh [options]
#   -f, --foreground    前台运行（不以守护进程方式运行）
#   -d, --debug         调试模式（日志级别设为 DEBUG）
#   -r, --root-dir      指定根目录
#   -h, --help          显示帮助信息
#

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 默认配置
UNIMRCP_SERVER="${ROOT_DIR}/bin/unimrcpserver"
PID_FILE="${ROOT_DIR}/var/unimrcpserver.pid"
LOG_DIR="${ROOT_DIR}/log"
CONF_DIR="${ROOT_DIR}/conf"

# 默认选项
FOREGROUND=0
DEBUG=0
CUSTOM_ROOT=""

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
    echo "UniMRCP Server 启动脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -f, --foreground    前台运行（不以守护进程方式运行）"
    echo "  -d, --debug         调试模式（日志级别设为 DEBUG）"
    echo "  -r, --root-dir DIR  指定 UniMRCP 根目录"
    echo "  -h, --help          显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                  # 以守护进程方式启动"
    echo "  $0 -f               # 前台运行"
    echo "  $0 -d               # 调试模式启动"
    echo "  $0 -r /opt/unimrcp  # 指定根目录启动"
    echo ""
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -f|--foreground)
                FOREGROUND=1
                shift
                ;;
            -d|--debug)
                DEBUG=1
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

# 检查服务器是否已运行
is_running() {
    if [ -f "$PID_FILE" ]; then
        local pid=$(cat "$PID_FILE")
        if ps -p "$pid" > /dev/null 2>&1; then
            return 0
        else
            # PID 文件存在但进程不存在，清理 PID 文件
            rm -f "$PID_FILE"
        fi
    fi
    return 1
}

# 查找服务器可执行文件
find_server() {
    # 如果指定了自定义根目录
    if [ -n "$CUSTOM_ROOT" ]; then
        ROOT_DIR="$CUSTOM_ROOT"
        UNIMRCP_SERVER="${ROOT_DIR}/bin/unimrcpserver"
        PID_FILE="${ROOT_DIR}/var/unimrcpserver.pid"
        LOG_DIR="${ROOT_DIR}/log"
        CONF_DIR="${ROOT_DIR}/conf"
    fi

    # 检查多个可能的位置
    local possible_paths=(
        "${ROOT_DIR}/bin/unimrcpserver"
        "${ROOT_DIR}/build/bin/unimrcpserver"
        "${ROOT_DIR}/platforms/unimrcp-server/unimrcpserver"
        "/usr/local/unimrcp/bin/unimrcpserver"
        "/opt/unimrcp/bin/unimrcpserver"
    )

    for path in "${possible_paths[@]}"; do
        if [ -x "$path" ]; then
            UNIMRCP_SERVER="$path"
            return 0
        fi
    done

    return 1
}

# 创建必要的目录
create_directories() {
    mkdir -p "${ROOT_DIR}/var" 2>/dev/null || true
    mkdir -p "${ROOT_DIR}/log" 2>/dev/null || true
}

# 启动服务器
start_server() {
    print_info "正在启动 UniMRCP Server..."

    # 查找服务器
    if ! find_server; then
        print_error "找不到 unimrcpserver 可执行文件"
        print_error "请先编译项目或指定正确的根目录"
        exit 1
    fi

    # 检查是否已运行
    if is_running; then
        local pid=$(cat "$PID_FILE")
        print_warn "UniMRCP Server 已经在运行中 (PID: $pid)"
        exit 0
    fi

    # 创建必要的目录
    create_directories

    # 构建启动命令
    local cmd="$UNIMRCP_SERVER"
    local args=()

    # 添加根目录参数
    args+=("-r" "$ROOT_DIR")

    # 调试模式
    if [ $DEBUG -eq 1 ]; then
        args+=("-l" "7")  # DEBUG 级别
        args+=("-o" "3")  # 控制台和文件都输出
    fi

    # 前台/后台模式
    if [ $FOREGROUND -eq 1 ]; then
        print_info "以前台模式运行..."
        print_info "按 Ctrl+C 停止服务器"
        echo ""
        exec "$cmd" "${args[@]}"
    else
        # 守护进程模式
        args+=("-d")  # daemon 模式
        args+=("-w")  # 不使用命令行
        
        "$cmd" "${args[@]}" &
        local pid=$!
        
        # 等待一下确保服务启动
        sleep 2
        
        if ps -p "$pid" > /dev/null 2>&1; then
            echo "$pid" > "$PID_FILE"
            print_info "UniMRCP Server 已启动 (PID: $pid)"
            print_info "PID 文件: $PID_FILE"
            print_info "日志目录: $LOG_DIR"
        else
            print_error "UniMRCP Server 启动失败"
            print_error "请检查日志文件获取详细信息: $LOG_DIR"
            exit 1
        fi
    fi
}

# 主函数
main() {
    parse_args "$@"
    start_server
}

main "$@"
