FROM phusion/baseimage:bionic-1.0.0
LABEL maintainer="pacprotocol Developers <dev@pacprotocol.org>"
LABEL description="Dockerised pacprotocolCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /pacprotocol

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} pacprotocol
RUN useradd -u ${USER_ID} -g pacprotocold -s /bin/bash -m -d /pacprotocold pacprotocol
RUN mkdir /pacprotocol/.dashcore
RUN chown pacprotocol:pacprotocol -R /pacprotocol

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER pacprotocol

VOLUME ["/pacprotocol"]

EXPOSE 9998 9999 19998 19999

WORKDIR /pacprotocol
