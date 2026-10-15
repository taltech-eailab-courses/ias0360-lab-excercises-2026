FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        libusb-1.0-0-dev \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        usbutils \
        ca-certificates \
        xxd \
        libglib2.0-0 \
        libgomp1 \
        openssh-client \
    && rm -rf /var/lib/apt/lists/*


ENV VIRTUAL_ENV=/opt/venv
RUN python3 -m venv "${VIRTUAL_ENV}"
ENV PATH="${VIRTUAL_ENV}/bin:${PATH}"

RUN pip install --no-cache-dir \
        pyserial \
        numpy \
        pandas \
        matplotlib \
        pillow \
        scikit-learn \
        opencv-python-headless \
        tensorflow \
        tensorflow-model-optimization \
        tqdm \
        jupyterlab \
        ipykernel

ENV JUPYTER_CONFIG_DIR=/opt/jupyter/config \
    JUPYTER_DATA_DIR=/opt/jupyter/data \
    JUPYTER_RUNTIME_DIR=/opt/jupyter/runtime
RUN mkdir -p "${JUPYTER_CONFIG_DIR}" "${JUPYTER_DATA_DIR}" "${JUPYTER_RUNTIME_DIR}"

ENV PICO_SDK_VERSION=2.3.0
ENV PICO_SDK_PATH=/opt/pico-sdk
RUN git clone --branch "${PICO_SDK_VERSION}" --depth 1 --recurse-submodules \
        https://github.com/raspberrypi/pico-sdk.git "${PICO_SDK_PATH}"

ENV PICOTOOL_VERSION=2.3.0
RUN git clone --branch "${PICOTOOL_VERSION}" --depth 1 https://github.com/raspberrypi/picotool.git /opt/picotool \
    && cmake -S /opt/picotool -B /opt/picotool/build -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
    && cmake --build /opt/picotool/build --target install -j"$(nproc)" \
    && ldconfig \
    && rm -rf /opt/picotool/build

WORKDIR /root

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod 755 /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

EXPOSE 8888
CMD ["/bin/bash"]
