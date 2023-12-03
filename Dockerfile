FROM ubuntu:latest
ENV DEBIAN_FRONTEND noninteractive

COPY . /app

# Install tools and make /app directory
RUN apt-get update \
	&& apt-get -y upgrade \
	&& apt-get install -y build-essential git ssh cmake \
	&& mkdir /app

# Change root password to allow login
RUN echo 'root:password' | chpasswd

# Set up ssh and configure it
RUN apt-get install -y ssh \
	&& ( \
		echo 'LogLevel DEBUG2'; \
		echo 'PermitRootLogin yes'; \
		echo 'PasswordAuthentication yes'; \
		echo 'Subsystem sftp /usr/lib/openssh/sftp-server'; \
	) > /etc/ssh/sshd_config_test_clion \
	&& mkdir /run/sshd

# Expose ssh port
EXPOSE 22

# Start ssh server
CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/etc/ssh/sshd_config_test_clion"]