#!/bin/bash
#
# UniMRCP Server 安装脚本
#
# 用法: ./install.sh [options]
#   -p, --prefix DIR    安装目录 (默认: /usr/local/unimrcp)
#   -h, --help          显示帮助信息
#

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 默认安装目录
PREFIX="/usr/local/unimrcp"

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
    echo "UniMRCP Server 安装脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -p, --prefix DIR  安装目录 (默认: /usr/local/unimrcp)"
    echo "  -h, --help        显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  sudo $0                       # 安装到默认目录"
    echo "  sudo $0 -p /opt/unimrcp       # 安装到 /opt/unimrcp"
    echo ""
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -p|--prefix)
                PREFIX="$2"
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

# 检查是否以 root 权限运行
check_root() {
    if [ "$EUID" -ne 0 ] && [[ "$PREFIX" == /usr/* || "$PREFIX" == /opt/* ]]; then
        print_error "安装到系统目录需要 root 权限，请使用 sudo 运行"
        exit 1
    fi
}

# 检查构建目录
check_build() {
    local build_dir="${ROOT_DIR}/build"
    
    if [ ! -d "$build_dir" ] || [ ! -f "${build_dir}/Makefile" ]; then
        print_warn "未找到构建目录，需要先编译项目"
        print_info "正在创建构建目录并编译..."
        
        mkdir -p "$build_dir"
        cd "$build_dir"
        
        cmake .. -DCMAKE_INSTALL_PREFIX="$PREFIX"
        make -j$(nproc)
        
        cd "$ROOT_DIR"
    fi
}

# 执行安装
do_install() {
    print_info "安装 UniMRCP Server 到 $PREFIX"
    
    local build_dir="${ROOT_DIR}/build"
    
    cd "$build_dir"
    
    # 执行安装
    make install
    
    cd "$ROOT_DIR"
    
    # 创建必要的目录
    mkdir -p "${PREFIX}/var"
    mkdir -p "${PREFIX}/log"
    
    # 复制管理脚本
    print_info "安装管理脚本..."
    cp -f "${SCRIPT_DIR}/start.sh" "${PREFIX}/bin/"
    cp -f "${SCRIPT_DIR}/stop.sh" "${PREFIX}/bin/"
    cp -f "${SCRIPT_DIR}/restart.sh" "${PREFIX}/bin/"
    cp -f "${SCRIPT_DIR}/status.sh" "${PREFIX}/bin/"
    chmod +x "${PREFIX}/bin/"*.sh
    
    # 更新脚本中的路径
    sed -i "s|ROOT_DIR=\"\$(cd \"\${SCRIPT_DIR}/..\" && pwd)\"|ROOT_DIR=\"${PREFIX}\"|g" "${PREFIX}/bin/"*.sh 2>/dev/null || true
    
    print_info "安装完成!"
    echo ""
    echo "=========================================="
    echo "  UniMRCP Server 已安装到: $PREFIX"
    echo "=========================================="
    echo ""
    echo "使用方法:"
    echo "  启动服务: ${PREFIX}/bin/start.sh"
    echo "  停止服务: ${PREFIX}/bin/stop.sh"
    echo "  重启服务: ${PREFIX}/bin/restart.sh"
    echo "  查看状态: ${PREFIX}/bin/status.sh"
    echo ""
    echo "配置文件: ${PREFIX}/conf/unimrcpserver.xml"
    echo "日志目录: ${PREFIX}/log/"
    echo ""
}

# 主函数
main() {
    parse_args "$@"
    check_root
    check_build
    do_install
}

main "$@"
