#
FROM astibal/smithproxy:ubuntu18.04-0.9-base

LABEL org.smithproxy.docker.image="astibal/smithproxy:ubuntu18.04-0.9-run-debug-localsrc"

# Set the working directory to /app
WORKDIR /app

COPY smithproxy/ /smithproxy/

RUN apt -y install gdb && \
cd /smithproxy && \
mkdir build ; cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j `cat /proc/cpuinfo  | grep processor | wc -l` install

# Define environment variable

# Run smithproxy when the container launches
CMD echo "Starting smithproxy .... " && ( /etc/init.d/smithproxy start ) > /dev/null 2>&1 && sleep 10 && \
    echo "SSL MITM CA cert (add to trusted CAs):" && cat /etc/smithproxy/certs/default/ca-cert.pem && smithproxy_cli && bash
