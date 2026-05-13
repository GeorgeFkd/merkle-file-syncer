FROM quay.io/minio/minio

ENV MINIO_ROOT_USER=minioadmin
ENV MINIO_ROOT_PASSWORD=minioadmin

EXPOSE 9000 9001

CMD ["server","/data","--console-address",":9001"]
