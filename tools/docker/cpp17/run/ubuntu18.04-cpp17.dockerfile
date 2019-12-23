#
FROM astibal/smithproxy:cpp17-base

# Set the working directory to /app
WORKDIR /app


RUN git clone https://bitbucket.com/astibal/socle.git -b cpp17 socle && git clone https://bitbucket.com/astibal/smithproxy.git -b cpp17 smithproxy && \
cd smithproxy && mkdir build && cd build && cmake .. && make install

# Define environment variable

# Run smithproxy when the container launches
CMD echo "Starting smithproxy .... " && ( /etc/init.d/smithproxy start ) > /dev/null 2>&1 && sleep 10 && \
    echo "SSL MITM CA cert (add to trusted CA's):" && cat /etc/smithproxy/certs/default/ca-cert.pem && smithproxy_cli && bash