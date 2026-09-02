# Local backup storage

Store migration backups here by date:

```
backups/
  YYYY-MM-DD/
    haplite-YYYY-MM-DD.backup      (gitignored — do not commit)
    haplite-reference.export
    hex-production.export
    router.json
    settings.json
    hotspot/                       (portal files from MikroTik)
```

Add `*.backup` to `.gitignore` if copying backups into this repo folder.
