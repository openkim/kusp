# base image
FROM ghcr.io/openkim/developer-platform

# set root user
USER root

# Install 
RUN apt-get -q update && \
    apt-get install -y --no-install-recommends curl unzip git zlib1g-dev


WORKDIR /opt/torchml_env

RUN curl https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-1.13.0%2Bcpu.zip --output libtorch.zip
RUN unzip libtorch.zip
ENV TORCH_ROOT="/opt/libtorch"

# Install TorchScatter/Sparse
RUN git clone --recurse-submodules https://github.com/rusty1s/pytorch_sparse
RUN git clone --recurse-submodules https://github.com/rusty1s/pytorch_scatter
RUN mkdir build_sparse build_scatter
RUN cd build_sparse \
    && cmake ../pytorch_sparse -DCMAKE_PREFIX_PATH=/opt/libtorch \
    && make install \
    && cd ../
RUN cd build_scatter \
    && cmake ../pytorch_scatter -DCMAKE_PREFIX_PATH=/opt/libtorch \
    && make install \
    && cd ../
# Ensure that correct "INTERFACE_INCLUDE_DIRECTORIES" var is set in TorchSparse TorchScatter
# For some reason it hardcode the initial folder
RUN sed -i "s#/opt/pytorch_sparse#/usr/local#g" /usr/local/share/cmake/TorchSparse/TorchSparseTargets.cmake
RUN sed -i "s#/opt/pytorch_scatter#/usr/local#g" /usr/local/share/cmake/TorchScatter/TorchScatterTargets.cmake
ENV TorchScatter_ROOT="/usr/local"
ENV TorchSparse_ROOT="/usr/local"

# Cleanup
RUN rm /opt/libtorch.zip
RUN rm -rf /opt/enzyme
RUN rm -rf pytorch_sparse pytorch_scatter build_scatter build_sparse

# Switch back to openkim env
WORKDIR /home/openkim
USER openkim
