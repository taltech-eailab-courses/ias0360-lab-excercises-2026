#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="ias0360-2026"
CONTAINER_NAME="ias0360-2026"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_CONTEXT="${SCRIPT_DIR}/own_pc_setup"
CONTAINER_HOME="/root"

usage() {
    echo "Usage: $0 [rebuild|shell|stop]"
    echo "  (no args)  build the image if missing, then run a fresh container"
    echo "             (kills and replaces any container already using this name,"
    echo "             even a still-running one - never attaches silently)"
    echo "  rebuild    stop+remove existing containers/image, rebuild, then run a fresh container"
    echo "  shell      open an additional terminal into the container you already have"
    echo "             running (e.g. to run Jupyter and Pico builds side by side);"
    echo "             fails if none is running - start one with a plain '$0' first"
    echo "  stop       stop the running container, if any, and exit"
    exit 1
}

build_image() {
    echo "Building Docker image '${IMAGE_NAME}'..."
    docker build -t "${IMAGE_NAME}" "${DOCKER_CONTEXT}"
}

ARG="${1:-}"
if [[ -n "${ARG}" && "${ARG}" != "rebuild" && "${ARG}" != "stop" && "${ARG}" != "shell" ]]; then
    usage
fi

if [[ "${ARG}" == "shell" ]]; then
    if ! docker ps --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
        echo "No running container named '${CONTAINER_NAME}'. Start one with '$0' first."
        exit 1
    fi

    running_mount_src="$(docker inspect -f '{{range .Mounts}}{{if eq .Destination "'"${CONTAINER_HOME}"'"}}{{.Source}}{{end}}{{end}}' "${CONTAINER_NAME}")"
    if [[ "${running_mount_src}" != "${SCRIPT_DIR}" ]]; then
        echo "A container named '${CONTAINER_NAME}' is running, but it's bound to a different directory - it isn't yours, refusing to attach."
        echo "If it's an abandoned session from someone else, a plain '$0' will replace it."
        exit 1
    fi

    exec docker exec -it --user "$(id -u):$(id -g)" "${CONTAINER_NAME}" bash
fi

if [[ "${ARG}" == "stop" ]]; then
    if docker ps --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
        echo "Stopping container '${CONTAINER_NAME}'..."
        docker stop "${CONTAINER_NAME}" >/dev/null
    else
        echo "Container '${CONTAINER_NAME}' is not running."
    fi
    exit 0
fi

if [[ "${ARG}" == "rebuild" ]]; then
    echo "Rebuild requested: removing existing containers and image..."

    # `mapfile` is a Bash 4+ builtin, unavailable on macOS's stock /bin/bash
    # (still 3.2, Apple's last GPLv2 release) - build the array by hand
    # instead so this keeps working there too.
    existing_containers=()
    while IFS= read -r container_id; do
        [[ -n "${container_id}" ]] && existing_containers+=("${container_id}")
    done < <(docker ps -aq --filter "ancestor=${IMAGE_NAME}")
    if [[ ${#existing_containers[@]} -gt 0 ]]; then
        docker rm -f "${existing_containers[@]}" >/dev/null
    fi
    if docker ps -a --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
        docker rm -f "${CONTAINER_NAME}" >/dev/null
    fi
    if docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
        docker rmi -f "${IMAGE_NAME}" >/dev/null
    fi

    build_image
fi

if ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
    build_image
fi


if docker ps -a --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
    docker rm -f "${CONTAINER_NAME}" >/dev/null
fi

echo "Creating and starting container '${CONTAINER_NAME}'..."
exec docker run -it --rm \
    --name "${CONTAINER_NAME}" \
    --privileged \
    -e "HOST_UID=$(id -u)" \
    -e "HOST_GID=$(id -g)" \
    -v "${SCRIPT_DIR}:${CONTAINER_HOME}" \
    -v /Volumes:/Volumes \
    -v /dev/bus/usb:/dev/bus/usb \
    -w "${CONTAINER_HOME}" \
    -p 8888:8888 \
    "${IMAGE_NAME}" \
    bash
