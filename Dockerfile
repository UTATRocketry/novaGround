FROM ubuntu:latest
ENV DEBIAN_FRONTEND noninteractive

# Install tools
RUN apt-get update \
	&& apt-get -y upgrade \
	&& apt-get install -y build-essential git ssh cmake

# Install xrdp and configure it properly
RUN apt-get install -y xrdp \
	&& cp /etc/xrdp/xrdp.ini /etc/xrdp/xrdp.ini.bak \
	&& sed -i 's/3389/3390/g' /etc/xrdp/xrdp.ini \
	&& sed -i 's/max_bpp=32/#max_bpp=32\nmax_bpp=128/g' /etc/xrdp/xrdp.ini \
	&& sed -i 's/xserverbpp=24/#xserverbpp=24\nxserverbpp=128/g' /etc/xrdp/xrdp.ini \
	&& sed -i "$ d" /etc/xrdp/startwm.sh && sed -i "$ d" /etc/xrdp/startwm.sh && echo 'startxfce4' >> /etc/xrdp/startwm.sh

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

# Expose ssh and xrdp ports
EXPOSE 22
EXPOSE 3390

# Start ssh server
CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/etc/ssh/sshd_config_test_clion"]