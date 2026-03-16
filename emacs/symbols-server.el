;;; symbols-server.el --- Emacs client for the C++ symbols server -*- lexical-binding: t; -*-

;;; Commentary:
;; Client for the external symbols.exe server.  Communicates over
;; JSON-over-stdio, one JSON object per line.
;;
;; Protocol (server-side fuzzy search, returns top-N results):
;;   Request:  {"id":N,"method":"query","params":{"pattern":"...","limit":200}}
;;   Response: {"id":N,"symbols":[{"name":"...","kind":"...","file":"...","line":N,"score":N},...]}
;;
;;   Single-file rebuild (on save):
;;   Request:  {"id":N,"method":"rebuildFile","params":{"file":"/abs/path/to/file.cpp"}}
;;   Response: {"id":N,"status":"rebuilt"}
;;
;; On startup the server emits an id:0 status line once the index is ready:
;;   {"id":0,"status":"ready","files":171,"symbols":3015}
;;
;; Usage:
;;   M-s M-s  -> symbols-server-find-symbols
;;   C-u M-s M-s -> force index rebuild before searching
;;   M-.      -> xref definitions  (via xref backend)
;;   M-,      -> xref-go-back  (standard xref; returns from M-. jump)

;;; Code:

(require 'consult)
(require 'orderless)
(require 'projectile)
(require 'json)
(require 'cl-lib)
(require 'xref)

;; ---------------------------------------------------------------------------
;; Customization

(defcustom symbols-server-executable
  (or (executable-find "symbols") "symbols")
  "Path to the symbols server executable.
Must be on PATH as \"symbols\" (or \"symbols.exe\" on Windows)."
  :type 'string
  :group 'symbols-server)

(defcustom symbols-server-default-limit 200
  "Maximum number of symbols returned per query."
  :type 'integer
  :group 'symbols-server)

(defcustom symbols-server-debounce 0.15
  "Seconds of idle time before sending a query to the server."
  :type 'float
  :group 'symbols-server)

(defcustom symbols-server-preview-delay 0.5
  "Seconds of idle time before loading a file for preview."
  :type 'float
  :group 'symbols-server)

(defcustom symbols-server-ready-timeout 300
  "Seconds to wait for the server to emit its id:0 ready line."
  :type 'integer
  :group 'symbols-server)

(defcustom symbols-server-auto-rebuild-on-save t
  "If non-nil, send a rebuildFile request whenever a C/C++ file in the project is saved.
Only the saved file is re-parsed; the rest of the index is untouched.
Set to nil to disable automatic single-file rebuilds on save."
  :type 'boolean
  :group 'symbols-server)

;; ---------------------------------------------------------------------------
;; Per-project server state

(cl-defstruct symbols-server--state
  process        ; the live process object
  project-root   ; string — project root this server was started for
  ready          ; nil until id:0 status line received
  pending        ; alist of (id . callback) for in-flight requests
  next-id        ; integer, next request id to use (starts at 1)
  partial        ; string, partial line received but not yet newline-terminated
  )

;; Hash table: project-root (string) -> symbols-server--state
(defvar symbols-server--servers (make-hash-table :test 'equal))

;; ---------------------------------------------------------------------------
;; Process management

(defun symbols-server--process-filter (proc string)
  "Handle STRING received from PROC."
  (let* ((root (process-get proc 'project-root))
         (state (gethash root symbols-server--servers)))
    (if (not state)
        (message "symbols-server: received data for unknown project %s" root)
      ;; Accumulate partial line
      (let ((buf (concat (or (symbols-server--state-partial state) "") string)))
        (setf (symbols-server--state-partial state) buf)
        ;; Process all complete lines
        (let ((start 0))
          (while (string-match "\n" buf start)
            (let* ((end (match-beginning 0))
                   (line (substring buf start end)))
              (setq start (match-end 0))
              (symbols-server--handle-line state line)))
          ;; Keep any remaining partial data
          (setf (symbols-server--state-partial state) (substring buf start)))))))

(defun symbols-server--handle-line (state line)
  "Parse one complete JSON LINE and dispatch to the right callback."
  (when (string-prefix-p "\r" line)
    (setq line (substring line 1)))
  (unless (string= line "")
    (condition-case err
        (let* ((obj (json-parse-string line :object-type 'alist :null-object nil))
               (id  (alist-get 'id obj)))
          (cond
           ;; id:0 is the startup ready notification
           ((equal id 0)
            (setf (symbols-server--state-ready state) t)
            (let ((files   (alist-get 'files   obj))
                  (symbols (alist-get 'symbols obj)))
              (message "symbols-server: ready (%s files, %s symbols)"
                       (or files "?") (or symbols "?"))))
           ;; Any other id: look up and call the registered callback
           (t
            (let* ((pending (symbols-server--state-pending state))
                   (entry   (assoc id pending)))
              (when entry
                (setf (symbols-server--state-pending state)
                      (cl-remove id pending :key #'car :test #'equal))
                (funcall (cdr entry) obj))))))
      (error
       (message "symbols-server: error parsing response: %s | line: %s"
                (error-message-string err) line)))))

(defun symbols-server--process-sentinel (proc event)
  "Handle PROC lifecycle EVENT."
  (let* ((root  (process-get proc 'project-root))
         (state (gethash root symbols-server--servers)))
    (when (and state (not (process-live-p proc)))
      (message "symbols-server: process exited (%s) for %s"
               (string-trim event) root)
      ;; Cancel all pending callbacks with an error
      (dolist (entry (symbols-server--state-pending state))
        (condition-case nil
            (funcall (cdr entry) nil)
          (error nil)))
      (remhash root symbols-server--servers))))

(defun symbols-server--get-or-start (project-root &optional search-dir force-rebuild)
  "Return a live server state for PROJECT-ROOT, starting one if necessary.
SEARCH-DIR, if non-nil, is passed as --search-dir.
If FORCE-REBUILD is non-nil, sends a rebuild request once ready."
  (let ((state (gethash project-root symbols-server--servers)))
    (if (and state (process-live-p (symbols-server--state-process state)))
        state
      ;; Start a new server process
      (let* ((exe    symbols-server-executable)
             (args   (list "--root" (expand-file-name project-root)))
             (args   (if search-dir
                         (append args (list "--search-dir" (expand-file-name search-dir)))
                       args))
             (proc   (make-process
                      :name     "symbols-server"
                      :command  (cons exe args)
                      :filter   #'symbols-server--process-filter
                      :sentinel #'symbols-server--process-sentinel
                      ;; Discard stderr — it carries only log lines, not protocol data.
                      :stderr   (make-pipe-process
                                 :name   "symbols-server-stderr"
                                 :filter (lambda (_proc string)
                                           (message "symbols-server log: %s" (string-trim string)))
                                 :noquery t)))
             (new-state (make-symbols-server--state
                         :process     proc
                         :project-root project-root
                         :ready       nil
                         :pending     nil
                         :next-id     1
                         :partial     "")))
        (process-put proc 'project-root project-root)
        (set-process-query-on-exit-flag proc nil)
        (puthash project-root new-state symbols-server--servers)
        (when force-rebuild
          ;; Queue a rebuild once the process is live (it rebuilds on first run anyway,
          ;; but if we want explicit rebuild after-ready we register a callback on id:0)
          (symbols-server--send-rebuild new-state))
        new-state))))

(defun symbols-server--wait-ready (state timeout-secs)
  "Block (with `accept-process-output') until STATE is ready or TIMEOUT-SECS elapsed.
Returns non-nil if the server became ready."
  (let ((deadline (+ (float-time) timeout-secs)))
    (while (and (not (symbols-server--state-ready state))
                (< (float-time) deadline)
                (process-live-p (symbols-server--state-process state)))
      (accept-process-output (symbols-server--state-process state) 0.1)))
  (symbols-server--state-ready state))

;; ---------------------------------------------------------------------------
;; Sending requests

(defun symbols-server--send (state json-string)
  "Send JSON-STRING (a single line) to the server in STATE."
  (let ((proc (symbols-server--state-process state)))
    (when (process-live-p proc)
      (process-send-string proc (concat json-string "\n")))))

(defun symbols-server--next-id (state)
  "Return the next request id for STATE and increment it."
  (let ((id (symbols-server--state-next-id state)))
    (setf (symbols-server--state-next-id state) (1+ id))
    id))

(defun symbols-server--send-query (state pattern limit callback)
  "Send a query for PATTERN (limit LIMIT) to STATE; call CALLBACK with the response alist."
  (let* ((id  (symbols-server--next-id state))
         (msg (json-serialize `((id     . ,id)
                                (method . "query")
                                (params . ((pattern . ,pattern)
                                           (limit   . ,limit)))))))
    (setf (symbols-server--state-pending state)
          (cons (cons id callback)
                (symbols-server--state-pending state)))
    (symbols-server--send state msg)))

(defun symbols-server--send-rebuild (state)
  "Send a full incremental rebuild request to STATE."
  (let* ((id  (symbols-server--next-id state))
         (msg (json-serialize `((id . ,id) (method . "rebuild")))))
    (symbols-server--send state msg)))

(defun symbols-server--send-rebuild-file (state file)
  "Send a single-file rebuild request for FILE to STATE.
FILE must be an absolute path string."
  (let* ((id  (symbols-server--next-id state))
         (msg (json-serialize `((id . ,id)
                                (method . "rebuildFile")
                                (params . ((file . ,file)))))))
    (symbols-server--send state msg)))

(defun symbols-server--send-shutdown (state)
  "Send a shutdown request to STATE."
  (let* ((id  (symbols-server--next-id state))
         (msg (json-serialize `((id . ,id) (method . "shutdown")))))
    (symbols-server--send state msg)))

;; ---------------------------------------------------------------------------
;; Symbol display helpers

(defun symbols-server--truncate-filepath (path)
  "Return PATH truncated to at most 50 characters.
If longer than 50 chars, return \"...\" followed by the last 47 characters,
preserving the most significant (trailing) portion of the path."
  (if (> (length path) 50)
      (concat "..." (substring path (- (length path) 47)))
    path))

(defun symbols-server--format-candidate (sym)
  "Format symbol alist SYM into a plain display string for consult.
Orderless highlighting is applied separately after formatting."
  (let* ((name     (alist-get 'name sym "?"))
         (kind     (alist-get 'kind sym "?"))
         (file     (alist-get 'file sym ""))
         (line     (alist-get 'line sym 0))
         (filepath (symbols-server--truncate-filepath file)))
    (format "%-50s %-10s %s:%s" name kind filepath line)))

(defun symbols-server--highlight-candidates (pattern candidates)
  "Apply orderless match highlighting for PATTERN to each string in CANDIDATES.
Returns a new list of strings with face text properties set."
  (when (and candidates (not (string= pattern "")))
    (orderless-highlight-matches pattern candidates)))

;; ---------------------------------------------------------------------------
;; Navigation / preview (mirrors treesit-utils behavior)

(defun symbols-server--goto-sym (sym &optional project-root)
  "Navigate to the location of SYM (an alist from the server response).
PROJECT-ROOT, if non-nil, is prepended to relative file paths."
  (when sym
    (let* ((file-raw (alist-get 'file sym))
           (file (if (and project-root file-raw
                          (not (file-name-absolute-p file-raw)))
                     (expand-file-name file-raw project-root)
                   file-raw))
           (line (alist-get 'line sym)))
      (when (and file line (file-exists-p file))
        (xref-push-marker-stack)
        (find-file file)
        (goto-char (point-min))
        (forward-line (1- line))
        (recenter)))))

(defun symbols-server--make-state-fn (project-root)
  "Return a consult state function for preview/navigation.
PROJECT-ROOT is prepended to relative file paths from the server."
  (let ((open-buffers (buffer-list))
        (preview-window (selected-window))
        (preview-timer nil))
    (lambda (action cand)
      ;; Cancel any pending preview timer
      (when preview-timer
        (cancel-timer preview-timer)
        (setq preview-timer nil))
      (cond
       ((eq action 'preview)
        (when cand
          (let* ((props (get-text-property 0 'symbols-server--sym cand))
                 (file-raw (and props (alist-get 'file props)))
                 (file (if (and project-root file-raw
                                (not (file-name-absolute-p file-raw)))
                           (expand-file-name file-raw project-root)
                         file-raw))
                 (line  (and props (alist-get 'line props))))
            (when (and file line (file-exists-p file))
              (let ((existing-buf (get-file-buffer file)))
                (if existing-buf
                    (progn
                      (with-current-buffer existing-buf
                        (goto-char (point-min))
                        (forward-line (1- line)))
                      (set-window-buffer preview-window existing-buf))
                  (setq preview-timer
                        (run-with-idle-timer
                         symbols-server-preview-delay nil
                         (lambda ()
                           (when (and (window-live-p preview-window)
                                      (file-exists-p file))
                              (let ((buf (find-file-noselect file)))
                                (with-current-buffer buf
                                  (goto-char (point-min))
                                  (forward-line (1- line)))
                                 (set-window-buffer preview-window buf))))))))))))
       ((eq action 'exit)
        ;; Close buffers opened solely for preview
        (dolist (buf (buffer-list))
          (unless (memq buf open-buffers)
            (when (buffer-file-name buf)
              (kill-buffer buf)))))))))


;; ---------------------------------------------------------------------------
;; Consult async source

(defun symbols-server--async-source (state)
  "Return a consult async function that queries server STATE."
  (let ((current-timer nil)
        (last-pattern nil))
    (lambda (action)
      (pcase action
        ;; Called with a string: new input from the user
        ((pred stringp)
         (let ((pattern action))
           ;; Debounce: cancel previous timer and start a new one
           (when current-timer
             (cancel-timer current-timer)
             (setq current-timer nil))
           (setq current-timer
                 (run-with-idle-timer
                  symbols-server-debounce nil
                  (lambda ()
                    (setq last-pattern pattern)
                    (symbols-server--send-query
                     state pattern symbols-server-default-limit
                     (lambda (response)
                       (when response
                         (let* ((syms (alist-get 'symbols response))
                                (candidates
                                 (when (sequencep syms)
                                   (mapcar (lambda (sym)
                                             (let ((s (symbols-server--format-candidate sym)))
                                               (put-text-property
                                                0 (length s)
                                                'symbols-server--sym sym s)
                                               s))
                                           syms))))
                           (funcall action 'flush)
                           (dolist (c candidates)
                             (funcall action c)))))))))))
        ;; consult lifecycle actions (setup, flush, etc.) — ignore them
        (_ nil)))))

;; ---------------------------------------------------------------------------
;; Search-dir helper (mirrors treesit-utils logic for TnT project)

(defun symbols-server--search-dir (project-root)
  "Return the search-dir argument for PROJECT-ROOT, or nil for the whole project."
  (let ((name (file-name-nondirectory (directory-file-name project-root))))
    (when (string= name "TnT")
      (expand-file-name "Code/DICE/Extensions/BattlefieldOnline" project-root))))

;; ---------------------------------------------------------------------------
;; Auto-rebuild-on-save hook

(defconst symbols-server--cpp-extensions
  '("c" "cc" "cpp" "cxx" "h" "hpp" "hxx" "ixx")
  "File extensions treated as C/C++ source files for auto-rebuild purposes.")

(defun symbols-server--cpp-file-p (filename)
  "Return non-nil if FILENAME has a C/C++ extension."
  (when filename
    (let ((ext (file-name-extension filename)))
      (and ext (member (downcase ext) symbols-server--cpp-extensions)))))

(defun symbols-server--after-save-hook ()
  "Hook for `after-save-hook': re-parse the saved file in the server index.
Only fires when `symbols-server-auto-rebuild-on-save' is non-nil, the buffer
visits a C/C++ file, and a live server already exists for this project.
The absolute path of the saved file is sent so only that one file is re-parsed."
  (when (and symbols-server-auto-rebuild-on-save
             (buffer-file-name)
             (symbols-server--cpp-file-p (buffer-file-name)))
    (condition-case nil
        (let* ((root (and (fboundp 'projectile-project-root)
                          (projectile-project-root)))
               (state (and root (gethash root symbols-server--servers))))
          (when (and state
                     (symbols-server--state-ready state)
                     (process-live-p (symbols-server--state-process state)))
            (symbols-server--send-rebuild-file
             state
             (expand-file-name (buffer-file-name)))))
      ;; Swallow any error so hooks never break normal save behavior.
      (error nil))))

;; Register the hook globally so it fires in every buffer.
(add-hook 'after-save-hook #'symbols-server--after-save-hook)

;; ---------------------------------------------------------------------------
;; xref backend

(defun symbols-server--xref-backend ()
  "Return the symbols-server xref backend when a live server exists for this project."
  (condition-case nil
      (when (fboundp 'projectile-project-root)
        (let* ((root  (projectile-project-root))
               (state (gethash root symbols-server--servers)))
          (when (and state
                     (symbols-server--state-ready state)
                     (process-live-p (symbols-server--state-process state)))
            'symbols-server)))
    (error nil)))

;; Register our backend in xref's dispatch list.
;; It runs before etags/elisp but only activates when a live server exists.
(add-hook 'xref-backend-functions #'symbols-server--xref-backend)

(cl-defmethod xref-backend-identifier-at-point ((_backend (eql symbols-server)))
  "Return the symbol name at point for the symbols-server backend."
  (thing-at-point 'symbol t))

(cl-defmethod xref-backend-identifier-completion-table ((_backend (eql symbols-server)))
  "Return nil; we do not support identifier completion via xref."
  nil)

(cl-defmethod xref-backend-definitions ((_backend (eql symbols-server)) identifier)
  "Find definitions of IDENTIFIER using the symbols server."
  (condition-case err
      (let* ((root  (projectile-project-root))
             (state (gethash root symbols-server--servers)))
        (unless (and state
                     (symbols-server--state-ready state)
                     (process-live-p (symbols-server--state-process state)))
          (user-error "symbols-server: no live server for this project"))
        ;; Query synchronously (up to 3 s) with exact-match priority.
        (let ((results nil)
              (done nil))
          (symbols-server--send-query
           state identifier symbols-server-default-limit
           (lambda (response)
             (setq results response
                   done    t)))
          (let ((deadline (+ (float-time) 3.0)))
            (while (and (not done) (< (float-time) deadline))
              (accept-process-output (symbols-server--state-process state) 0.05)))
          ;; Convert matching symbols to xref-location objects.
          (when results
            (let ((syms (alist-get 'symbols results)))
              (when (sequencep syms)
                ;; Keep only symbols whose name matches the identifier exactly
                ;; (case-insensitive), then fall back to all results if none do.
                (let* ((exact (cl-remove-if-not
                                (lambda (sym)
                                  (string= (downcase (alist-get 'name sym ""))
                                           (downcase identifier)))
                                syms))
                       (candidates (if exact exact syms)))
                  (mapcar (lambda (sym)
                            (let* ((file-raw (alist-get 'file sym ""))
                                   (file (if (file-name-absolute-p file-raw)
                                             file-raw
                                           (expand-file-name file-raw root)))
                                   (line (alist-get 'line sym 1))
                                   (name (alist-get 'name sym ""))
                                   (kind (alist-get 'kind sym ""))
                                   (summary (format "%s [%s]" name kind)))
                              (xref-make summary
                                         (xref-make-file-location file line 0))))
                          candidates)))))))
    (error
     (message "symbols-server xref: %s" (error-message-string err))
     nil)))

(cl-defmethod xref-backend-references ((_backend (eql symbols-server)) _identifier)
  "References lookup is not supported; return nil to fall through to the next backend."
  nil)

(cl-defmethod xref-backend-apropos ((_backend (eql symbols-server)) pattern)
  "Return xref locations for all symbols matching PATTERN."
  (condition-case err
      (let* ((root  (projectile-project-root))
             (state (gethash root symbols-server--servers)))
        (unless (and state
                     (symbols-server--state-ready state)
                     (process-live-p (symbols-server--state-process state)))
          (user-error "symbols-server: no live server for this project"))
        (let ((results nil)
              (done nil))
          (symbols-server--send-query
           state pattern symbols-server-default-limit
           (lambda (response)
             (setq results response
                   done    t)))
          (let ((deadline (+ (float-time) 3.0)))
            (while (and (not done) (< (float-time) deadline))
              (accept-process-output (symbols-server--state-process state) 0.05)))
          (when results
            (let ((syms (alist-get 'symbols results)))
              (when (sequencep syms)
                (mapcar (lambda (sym)
                          (let* ((file-raw (alist-get 'file sym ""))
                                 (file (if (file-name-absolute-p file-raw)
                                           file-raw
                                         (expand-file-name file-raw root)))
                                 (line (alist-get 'line sym 1))
                                 (name (alist-get 'name sym ""))
                                 (kind (alist-get 'kind sym ""))
                                 (summary (format "%s [%s]" name kind)))
                            (xref-make summary
                                       (xref-make-file-location file line 0))))
                        syms))))))
    (error
     (message "symbols-server xref: %s" (error-message-string err))
     nil)))

;; ---------------------------------------------------------------------------
;; Entry point

;;;###autoload
(defun symbols-server-find-symbols (&optional force-rebuild)
  "Browse C++ symbols in the current project using the symbols server.
With prefix argument FORCE-REBUILD, trigger an index rebuild first."
  (interactive "P")
  (let* ((project-root (or (projectile-project-root)
                           (user-error "Not in a projectile project")))
         (search-dir   (symbols-server--search-dir project-root))
         (state        (symbols-server--get-or-start project-root search-dir))
         (_            (unless (symbols-server--wait-ready state symbols-server-ready-timeout)
                         (user-error "symbols-server: timed out waiting for server to become ready")))
         (_            (when force-rebuild
                         (message "symbols-server: requesting index rebuild...")
                         (symbols-server--send-rebuild state)
                         ;; Give it a moment so the rebuild ack comes back before we query
                         (sit-for 0.5))))
    (let* ((last-candidates nil)
           (collection
            (consult--dynamic-collection
             (lambda (input)
               (when (and input (not (string= input "")))
                 (let ((results nil)
                       (done nil))
                   (symbols-server--send-query
                    state input symbols-server-default-limit
                    (lambda (response)
                      (when response
                        (let ((syms (alist-get 'symbols response)))
                          (when (sequencep syms)
                            ;; Format, attach metadata, then highlight
                            (let* ((plain (mapcar (lambda (sym)
                                                    (let ((s (symbols-server--format-candidate sym)))
                                                      (put-text-property
                                                       0 (length s)
                                                       'symbols-server--sym sym s)
                                                      s))
                                                  syms))
                                   (highlighted (or (symbols-server--highlight-candidates input plain)
                                                    plain)))
                              ;; Copy metadata from plain strings to the highlighted copies
                              (setq results
                                    (cl-mapcar (lambda (orig hi)
                                                 (let ((meta (get-text-property 0 'symbols-server--sym orig)))
                                                   (put-text-property 0 (length hi) 'symbols-server--sym meta hi)
                                                   hi))
                                               plain highlighted))))))
                      (setq done t)))
                   ;; Wait synchronously up to 3 seconds for the response
                   (let ((deadline (+ (float-time) 3.0)))
                     (while (and (not done) (< (float-time) deadline))
                       (accept-process-output (symbols-server--state-process state) 0.05)))
                   (setq last-candidates results)
                   results)))
             :min-input 1))
           (selected
            (consult--read
             collection
             :prompt "C++ Symbol: "
             :category 'cpp-symbol
             :state (symbols-server--make-state-fn project-root)
             :lookup (lambda (selected _candidates _input _narrow)
                       ;; Find the original candidate string that carries the text property.
                       ;; consult may return a string copy without it, so search by display text.
                       (or (cl-find selected last-candidates :test #'string=)
                           selected))
             :require-match t
             :sort nil)))
      (when selected
        (let ((sym (get-text-property 0 'symbols-server--sym selected)))
          (symbols-server--goto-sym sym project-root))))))

;;;###autoload
(defun symbols-server-shutdown (&optional project-root)
  "Shut down the symbols server for PROJECT-ROOT (defaults to current project)."
  (interactive)
  (let* ((root  (or project-root
                    (projectile-project-root)
                    (user-error "Not in a projectile project")))
         (state (gethash root symbols-server--servers)))
    (if state
        (progn
          (symbols-server--send-shutdown state)
          (message "symbols-server: shutdown sent for %s" root))
      (message "symbols-server: no server running for %s" root))))

;;;###autoload
(defun symbols-server-restart ()
  "Kill and restart the symbols server for the current project."
  (interactive)
  (let* ((root  (or (projectile-project-root)
                    (user-error "Not in a projectile project")))
         (state (gethash root symbols-server--servers)))
    (when state
      (when (process-live-p (symbols-server--state-process state))
        (symbols-server--send-shutdown state)
        (sit-for 0.3)
        (delete-process (symbols-server--state-process state)))
      (remhash root symbols-server--servers))
    (symbols-server--get-or-start root (symbols-server--search-dir root))
    (message "symbols-server: restarted for %s" root)))

(provide 'symbols-server)
;;; symbols-server.el ends here
