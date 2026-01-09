FROM debian:12 AS builder

RUN apt update
RUN apt install -y build-essential libssl-dev libz-dev
WORKDIR /app
COPY . .
RUN make -j $(nproc)
RUN make install

FROM debian:12

RUN apt update
RUN apt install -y libssl3
COPY --from=builder /var/gigablast/data0 /proto
COPY --from=builder /var/gigablast/data0/gb /usr/bin/gb
COPY --from=builder /app/entrypoint.sh /usr/bin/entrypoint.sh

ENTRYPOINT ["entrypoint.sh"]
CMD ["gb"]
