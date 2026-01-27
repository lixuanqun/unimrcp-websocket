#!/bin/bash
#
# UniMRCP Server 重启脚本
#
# 用法: ./restart.sh [options]
#   -f, --foreground    前台运行（不以守护进程方式运行）
#   -d, --debug         调试模式（日志级别设为 DEBUG）
#   -r, --root-dir      指定根目录
#   --force             强制停止后重启
#   -h, --help          显示帮助信息
#

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 默认选项
FOREGROUND=0
DEBUG=0
FORCE=0
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
    echo "UniMRCP Server 重启脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -f, --foreground    前台运行（不以守护进程方式运行）"
    echo "  -d, --debug         调试模式（日志级别设为 DEBUG）"
    echo "  -r, --root-dir DIR  指定 UniMRCP 根目录"
    echo "  --force             强制停止后重启"
    echo "  -h, --help          显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                  # 重启服务"
    echo "  $0 -f               # 重启并前台运行"
    echo "  $0 --force          # 强制重启"
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
            --force)
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

# 重启服务器
restart_server() {
    print_info "正在重启 UniMRCP Server..."
    echo ""

    # 构建停止命令参数
    local stop_args=()
    if [ $FORCE -eq 1 ]; then
        stop_args+=("-f")
    fi
    if [ -n "$CUSTOM_ROOT" ]; then
        stop_args+=("-r" "$CUSTOM_ROOT")
    fi

    # 停止服务
    "${SCRIPT_DIR}/stop.sh" "${stop_args[@]}" || true

    echo ""
    sleep 2

    # 构建启动命令参数
    local start_args=()
    if [ $FOREGROUND -eq 1 ]; then
        start_args+=("-f")
    fi
    if [ $DEBUG -eq 1 ]; then
        start_args+=("-d")
    fi
    if [ -n "$CUSTOM_ROOT" ]; then
        start_args+=("-r" "$CUSTOM_ROOT")
    fi

    # 启动服务
    "${SCRIPT_DIR}/start.sh" "${start_args[@]}"
}

# 主函数
main() {
    parse_args "$@"
    restart_server
}

main "$@"
