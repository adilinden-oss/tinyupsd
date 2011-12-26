#!/bin/sh
echo -n ' tinyupsd'

case "$1" in
  start)
    /sbin/tinyupsd
    ;;
  stop)
    kill `cat /var/run/tinyupsd.pid`
    sleep 2
    if [ -f /var/run/tinyupsd.pid ]; then
        kill -9 `cat /var/run/tinyupsd.pid`
    fi
    ;;
  *)
    echo "Usage: `basename $0` {start|stop}" >&2
    exit 64
    ;;
esac

exit 0

