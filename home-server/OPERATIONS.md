# Проверки и восстановление

## Перед перезагрузкой

```bash
sudo findmnt --verify --verbose
findmnt --mountpoint /mnt/photos -o SOURCE,UUID,FSTYPE,OPTIONS,TARGET
sudo systemctl is-enabled docker
sudo docker ps
```

Исправить ошибки `fstab` до перезагрузки. Предусмотреть доступ к локальной консоли: неверная запись может прервать удалённую загрузку. Перезагрузка прерывает работу всех сервисов, включая Telegram-бота; выбрать подходящее время.

```bash
sudo reboot
```

После повторного подключения:

```bash
hostname
lsblk -f
findmnt --mountpoint /mnt/photos -o SOURCE,UUID,FSTYPE,OPTIONS,TARGET
df -hT / /mnt/photos
stat -c '%U:%G %n' /mnt/photos/immich
sudo systemctl is-active docker
sudo systemctl is-enabled docker
sudo docker ps -a
```

Ожидаются UUID из README, ext4 в `/mnt/photos`, `nick:nick`, Docker `active/enabled`. Сам по себе успешный `df /mnt/photos` ничего не доказывает: при отсутствии mount он покажет системную файловую систему. `hello-world` удалён флагом `--rm` и не обязан присутствовать в списке контейнеров. Проверка после перезагрузки пока не подтверждена.

## HDD не смонтирован

Не запускать Immich и не создавать новую библиотеку. Если он уже будет установлен, остановить его стек в каталоге Compose командой `sudo docker compose stop`.

```bash
lsblk -o NAME,SIZE,MODEL,FSTYPE,LABEL,UUID,MOUNTPOINTS
sudo blkid
sudo findmnt --verify --verbose
sudo systemctl status mnt-photos.mount --no-pager
sudo journalctl -b -u mnt-photos.mount --no-pager
sudo journalctl -k -b --no-pager
```

Проверить питание, кабели, UUID и запись `fstab`. После устранения причины:

```bash
sudo systemctl daemon-reload
sudo mount /mnt/photos
findmnt --mountpoint /mnt/photos -o SOURCE,UUID,FSTYPE,TARGET
```

Если до подключения диска приложение успело записать файлы в `/mnt/photos` на NVMe, не удалять их и не смешивать автоматически с HDD. Остановить приложение, сохранить обе копии и разбирать их отдельно: после mount файлы NVMe будут скрыты точкой монтирования.

В emergency mode через локальную консоль проверить диск и отредактировать только ошибочную запись `fstab`. При read-only корне сначала `mount -o remount,rw /`. Временно закомментированная запись позволит загрузиться без HDD, но Immich должен оставаться остановленным. Не восстанавливать весь старый `fstab` без сравнения: после создания копии могли появиться другие нужные записи.

При ошибках ext4 сначала обеспечить копию важных данных; `e2fsck` выполняется только на размонтированном разделе. Не запускать автоматическое исправление `-y` без оценки повреждений. При признаках физического отказа приоритет — спасение данных, а не нагрузочные тесты.

## Docker не запускается

```bash
sudo systemctl status docker --no-pager
sudo journalctl -b -u docker --no-pager -n 100
df -hT /
df -i /
```

После устранения причины выполнить `sudo systemctl start docker`. Перезапуск работающего Docker может затронуть другие контейнеры. Не удалять `/var/lib/docker`, volumes или базу в качестве универсального исправления. Не применять `docker compose down -v` при восстановлении данных.

## SMART

После проверки, что `/dev/sda` — нужный HDD:

```bash
sudo smartctl -H -A /dev/sda
sudo smartctl -t short /dev/sda
# Дождаться времени, указанного smartctl, затем:
sudo smartctl -l selftest /dev/sda
# Длинный тест запускать отдельно в период низкой нагрузки:
sudo smartctl -t long /dev/sda
# После завершения:
sudo smartctl -l selftest /dev/sda
```

Исходная оценка времени long test — 657 минут, short — 1 минута. Не считать тест завершённым сразу после команды запуска. Сохранять историю температуры и счётчиков 5/197/198; рост или ошибки требуют расследования и проверки резервной копии. Перед публикацией вывода удалить серийный номер и WWN.
