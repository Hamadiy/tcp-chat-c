# Chat TCP multi-clients en C

![CI](https://github.com/Hamadiy/tcp-chat-c/actions/workflows/ci.yml/badge.svg)
![Langage](https://img.shields.io/badge/langage-C11-blue)
![Plateforme](https://img.shields.io/badge/plateforme-Linux-lightgrey)

Un serveur de discussion TCP qui gère jusqu'à 64 clients avec **un seul processus et un seul thread**, grâce au multiplexage d'E/S avec `poll()`. Un client en ligne de commande est fourni, et on peut aussi se connecter avec `nc` ou `telnet`.

## Fonctionnalités

- **Multiplexage avec `poll()`** : le serveur ne bloque jamais sur un client lent.
- **Tampon de ligne par client** : un message peut arriver en plusieurs segments TCP. Les lectures partielles et les fins de ligne `\r\n` sont gérées.
- **Pseudos** validés et uniques.
- **Commandes** :
  - `/who` : liste des personnes connectées ;
  - `/quit` : quitter le chat.
- **Notifications** quand quelqu'un rejoint ou quitte le chat.
- **Robustesse** :
  - `SIGPIPE` est ignoré et les envois utilisent `MSG_NOSIGNAL`, donc un client qui coupe brutalement ne fait pas planter le serveur ;
  - au-delà de 64 clients, les nouvelles connexions sont refusées proprement.
- **Client** : surveille à la fois le clavier et le socket, ce qui affiche les messages reçus pendant la saisie. Il accepte IPv4 et IPv6.

## Compilation

```bash
make        # produit bin/chat_server et bin/chat_client
make test   # tests d'intégration (Python 3)
```

## Utilisation

```bash
./bin/chat_server -p 5000             # terminal 1
./bin/chat_client -p 5000 127.0.0.1   # terminal 2, 3, ...
nc 127.0.0.1 5000                     # ou avec netcat
```

```
Welcome to the chat! Choose a nickname:
alice
Hello alice! Commands: /who, /quit
* bob joined the chat
[bob] salut !
/who
Online: alice bob
```

## Architecture

```
                 ┌─────────────────────────────┐
 client 1 ──TCP──┤                             │
 client 2 ──TCP──┤  boucle poll()              │
   ...           │   ├─ listener prêt → accept │
 client N ──TCP──┤   └─ client prêt   → recv   │
                 │        └─ découpe en lignes │
                 │           └─ commande ou    │
                 │              broadcast      │
                 └─────────────────────────────┘
```

## Tests

`tests/test_chat.py` lance le serveur et ouvre de vrais sockets TCP. Il vérifie :
- l'accueil et le choix des pseudos, y compris le refus des pseudos invalides ou déjà pris ;
- la diffusion des messages et les annonces d'arrivée et de départ ;
- `/who` et les commandes inconnues ;
- un message envoyé en deux segments avec une fin de ligne `\r\n`.

## Auteur

**Younesse HAMADI** – étudiant en BUT Réseaux & Télécommunications.
