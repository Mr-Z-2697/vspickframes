so there's
```
    bool duplicateOffset = false;
    for (int i = 0; i < d->num; i++) {
        for (int j = i + 1; j < d->num; j++) {
            if (d->offsets[i] == d->offsets[j]) {
                duplicateOffset = true;
                break;
            }
        }
    }
```
in original SelectEvery, worst case scenario it runs num\*num times.
assuming there will be duplicates just doesn't really hurt what we are doing.
