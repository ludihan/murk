# Jenkins with the Docker CLI, compose and buildx plugins, so builds can drive the host's Docker daemon
# through the mounted socket
FROM docker:cli AS docker

FROM jenkins/jenkins:lts-jdk21
USER root
COPY --from=docker /usr/local/bin/docker /usr/local/bin/docker
COPY --from=docker /usr/local/libexec/docker/cli-plugins/ /usr/local/lib/docker/cli-plugins/
# the socket is root-owned, so Jenkins runs as root (it is root-equivalent on the host either way)
