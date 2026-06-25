# Interactive Collaborative Document Editor

A web-based collaborative document editor designed to support multi-user document creation, sharing, and editing through a flexible access-control system.

The project enables users to create documents, share them using access codes, manage viewing and editing permissions, and collaborate through a rich-text editing environment. It also introduces a hierarchical permission model for conflict resolution and document ownership management.

##  Live Demo

https://text-editor-44f76.web.app/

---

##  Features

### 📄 Document Management

* Create new documents
* Open and view existing documents
* Save and manage document content
* Access document history

###  Access Control System

* Access-code-based document sharing
* Separate View and Edit permissions
* Owner-controlled access revocation
* Permission validation before document access

###  Rich Text Editing

* Bold, Italic, Underline, Strikethrough
* Undo and Redo
* Font type and size controls
* Text color and highlight color
* Page color customization
* Alignment and paragraph formatting
* Lists and checklists
* Header and footer support

### 📎 Content Insertion

* Images
* Videos
* Audio files
* Hyperlinks
* Tables
* Charts
* Shapes and symbols
* Digital signatures

###  Additional Utilities

* Margin controls
* Zoom controls
* Word count
* Print support

###  Hierarchical Permission Model

* Owner-defined hierarchy levels
* Separate View and Edit access codes
* Priority-based conflict handling
* Access revocation by document owner

---

##  Workflow
![Editor Workflow](images/editor_screenshot.png)

### Access Existing Documents

1. User enters an access code.
2. The system validates the code.
3. The system checks whether access has been revoked.
4. Based on permission type:

   * View access is granted.
   * Edit access is granted.

### Create New Documents

1. User creates a new document.
2. The system generates:

   * View Code
   * Edit Code
3. Users can share these codes with collaborators.
4. The owner manages permissions and access levels.

### Conflict Resolution

#### General Mode

* If multiple users edit the same line, a priority system determines which changes are reflected.

#### Hierarchical Mode

* Users are assigned hierarchy levels.
* Higher-priority edits take precedence during conflicts.
* Priority tables are periodically regenerated for fairness among users at the same hierarchy level.

---

##  Tech Stack

* HTML
* CSS
* JavaScript
* Firebase
* WebSockets (integration in progress)

---

## 📊 Flowchart

The workflow and access-control architecture are illustrated in the project flowchart.

---

##  Integration Status

### Completed

* Authentication interface
* Home page and editor interface
* Access-code workflow
* Document creation system
* Rich-text editing features
* Permission management framework
* Hierarchical access design

### Under Integration

* Firebase user-system integration
* Automatic page routing
* WebSocket-based synchronization
* Real-time collaborative editing
* Backend-editor communication

---

##  Contributors

* Shradha Kedia
* Aratrika Roy
* Malini Goyal
* Stuti Prajapati
