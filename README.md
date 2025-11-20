# **WO Files – Ultra-Modern GTK File Manager 🗂️🔥**

WO Files is a **lightweight, blazing-fast, modern C/GTK file manager** featuring an **OLED-black UI**, smooth animations, custom icons, and a clean design inspired by macOS + KDE + futuristic cyberpunk vibes.

Built entirely in **C + GTK3**, it aims to be a minimal, efficient, aesthetic alternative to heavy file managers.

---

## **✨ Features**

🔥 **Modern OLED Black UI** – fully custom CSS theme

🎨 **Icon Pack Integration** – clean icons from Icons8

📁 **Icon Grid View** – fast, responsive, dynamic

🔍 **Smart Search** – instant filtering + deep search

📜 **History Navigation** – back / forward / up

✂️ **File Operations** – copy, cut, paste, rename, delete

🗑️ **Right-Click Context Menu**

🧩 **Auto-detects file type → loads correct icon**

📌 **Custom Sidebar Shortcuts**

🔒 **SUDO mode** (browse system directories instantly)

⚡ **Fast recursive scanning**

🖼️ **Smooth hover effects & card-style item view**

---

## **📸 Screenshots**

![Screenshot](assets/ss1.png)
![Screenshot](assets/ss2.png)

---

## **🛠️ Build Instructions**

### **Dependencies**

You need GTK3:

```bash
sudo apt install libgtk-3-dev
```

### **Clone & Build**

```bash
git clone https://github.com/dwip-the-dev/WO-Files.git
cd WO-Files
make clean
make
./wo-files
```

---

## **📂 Project Structure**

```
wo-files/
│── src/
│   ├── main.c
│   ├── explorer.c
│   ├── explorer.h
│   ├── ui.c
│   ├── ui.h
│   ├── utils.c
│   ├── utils.h
│── assets/
│   ├── icons... (PNG files)
│   ├── theme.css
│   ├── logo.png
│── Makefile
│── README.md
```

---

## **🎨 Icon Credits**

All icons used in this project are sourced from:

### **[Icons8](https://icons8.com/)**

Thanks to Icons8 for providing high-quality icons ♥️

---

## **🌟 Contribute**

PRs are welcome!
Feel free to submit:

* New UI ideas
* New features
* Bug fixes
* Themes
* Performance patches

---

## **💬 Author**

Built with ❤️ by **Dwip**
