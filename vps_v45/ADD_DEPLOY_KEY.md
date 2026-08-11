# Добавить deploy-ключ Cursor на VPS (один раз)

С вашей машины, где уже есть SSH к `151.247.208.17`:

```bash
PUB='ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIHhio+i6GDLcitq0g2OxivsBz4XC+MlsjUYhmT+oEmgw cursor-boiler-v45-deploy'

ssh root@151.247.208.17 "mkdir -p /root/.ssh && chmod 700 /root/.ssh &&
  grep -q 'cursor-boiler-v45-deploy' /root/.ssh/authorized_keys 2>/dev/null || echo '$PUB' >> /root/.ssh/authorized_keys &&
  chmod 600 /root/.ssh/authorized_keys && echo KEY_OK"
```

После этого агент сможет задеплоить стек сам.
