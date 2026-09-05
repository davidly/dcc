# Building Project Docs

The project documentation site lives under `docs/docs`. To preview it locally,
create a Python virtual environment in that directory, install the MkDocs
requirements, and run `mkdocs serve`. The local server prints the preview URL,
normally `http://127.0.0.1:8000/`.

Run the commands for your platform from the root of the DCC repo folder.

If you edit the docs in VS Code, install these Markdown extensions for a better
editing experience:

- [Markdown All in One](https://marketplace.visualstudio.com/items?itemName=yzhang.markdown-all-in-one){: target="_blank" }
- [markdownlint](https://marketplace.visualstudio.com/items?itemName=DavidAnson.vscode-markdownlint){: target="_blank" }

=== "Windows"

    ```powershell
    cd docs\docs
    py -m venv .venv
    .\.venv\Scripts\Activate.ps1
    python -m pip install --upgrade pip
    python -m pip install -r requirements.txt
    mkdocs serve
    ```

=== "macOS"

    ```bash
    cd docs/docs
    python3 -m venv .venv
    source .venv/bin/activate
    python -m pip install --upgrade pip
    python -m pip install -r requirements.txt
    mkdocs serve
    ```

=== "Ubuntu"

    ```bash
    cd docs/docs
    python3 -m venv .venv
    source .venv/bin/activate
    python -m pip install --upgrade pip
    python -m pip install -r requirements.txt
    mkdocs serve
    ```

=== "Ubuntu ARM64"

    ```bash
    cd docs/docs
    python3 -m venv .venv
    source .venv/bin/activate
    python -m pip install --upgrade pip
    python -m pip install -r requirements.txt
    mkdocs serve
    ```

=== "Windows ARM64"

    ```powershell
    cd docs\docs
    py -m venv .venv
    .\.venv\Scripts\Activate.ps1
    python -m pip install --upgrade pip
    python -m pip install -r requirements.txt
    mkdocs serve
    ```

After `mkdocs serve` starts, open `http://127.0.0.1:8000/` in your browser to
view the local documentation site. Keep the virtual environment active while
serving the site. As you edit the documentation files, MkDocs rebuilds the site
and refreshes the browser automatically. When you are done, press `Ctrl+C` to
stop `mkdocs serve`.

Before publishing, run `mkdocs build --strict` from `docs/docs`. This validates
generated header tables, local page links, and section anchors. The working
directory matters because source snippets use paths relative to it. The output
is `docs/site`; do not edit those generated files.
