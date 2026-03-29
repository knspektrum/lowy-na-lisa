### Github

---

1. __Pobieranie zmian__
    - `git fetch`
    - `git pull`

---

2. __Wrzucenie wszystkich zmian__
    - `git add .`

    Lub wybrany folder/plik
    - `git add .\pcb\`

---

3. __Status__
    `git status`

---

4. __Commit__
    `git commit -m` __"wiadomość"__

---

5. __Push__
    `git push`

    Jeżeli nie przejdzie to:
    `git push origin main`


6. __Merge__
    1. `git add .` `git commit -m "moje zmiany"`
    
    2. `git fetch`
    
    3. `git merge origin/main`
    
    4. Rozwiązać konflikty
    
    5. `git push origin main --force-with-lease`

---

7. __Revert__
    1. Znależć hash commita np: `3cdeabb6d4475bf22c77ce93f576f29bd75ad27a`

    2. `git revert 3cdeabb6d4475bf22c77ce93f576f29bd75ad27a`

    3. Klikamy ESC a potem __:wq__ i enter. (VIM)

    4. `git push`

