(function () {
    var btn = document.getElementById("themeBtn");
    if (!btn) return;

    function currentTheme() {
        var explicit = document.documentElement.getAttribute("data-theme");
        if (explicit === "light" || explicit === "dark") return explicit;
        return window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches
            ? "light"
            : "dark";
    }

    function applyTheme(next) {
        document.documentElement.setAttribute("data-theme", next);
        try {
            localStorage.setItem("theme", next);
        } catch (e) {}
    }

    btn.addEventListener("click", function () {
        applyTheme(currentTheme() === "dark" ? "light" : "dark");
    });
})();
