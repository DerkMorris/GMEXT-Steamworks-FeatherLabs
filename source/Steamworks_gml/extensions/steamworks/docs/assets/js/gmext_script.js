document.addEventListener('DOMContentLoaded', function() {

    // The checked-in documentation consists of pre-rendered MkDocs pages, so
    // each page has its own copy of the navigation. Keep the dedicated-server
    // entry available even on pages generated before that module was added.
    var menu = document.querySelector('.wy-menu.wy-menu-vertical');
    if (menu && !menu.querySelector('a[href="dedicated_servers.html"]')) {
        var generalLink = menu.querySelector('a[href="general.html"]');
        var list = document.createElement('ul');
        list.innerHTML = '<li class="toctree-l1"><a class="reference internal" href="dedicated_servers.html">Dedicated Servers</a></li>';
        if (generalLink && generalLink.closest('ul')) {
            generalLink.closest('ul').insertAdjacentElement('afterend', list);
        } else {
            menu.appendChild(list);
        }
    }
    
    // Top left link should point ot the github page
    var link = document.querySelector('a.icon.icon-home');
    var githublink = document.querySelector('a.fa.fa-github')
    if (link && githublink) {
        link.href = githublink.href;
    }

    // Point home icon to home.html
    var home_link = document.querySelector('a.icon.icon-home[aria-label="Docs"]');
    home_link.href = 'home.html'

    // Remove the editor icon
    var element = document.querySelector('li.wy-breadcrumbs-aside');
    if (element) {
        element.remove();
    }

    // Add copy-to-clipboard on code blocks
    const codeBlocks = document.querySelectorAll('pre code');
    codeBlocks.forEach(function(codeBlock) {
        const button = document.createElement('button');
        button.className = 'copy-code-button';
        button.type = 'button';
        button.innerHTML = '<i class="fas fa-clipboard"></i>'; // Clipboard icon
        button.addEventListener('click', function() {
            navigator.clipboard.writeText(codeBlock.innerText).then(() => {
                const originalInnerHTML = button.innerHTML;
                button.innerHTML = '<i class="fas fa-check"></i>'; // Check icon
                button.style.backgroundColor = "#28a745"; // Change button to green
                setTimeout(() => {
                    button.innerHTML = originalInnerHTML; // Reset icon
                    button.style.backgroundColor = ""; // Reset button color
                }, 2000); // Reset icon and button color after 2 seconds
            }, (err) => {
                console.error('Failed to copy text: ', err);
            });
        });

        const pre = codeBlock.parentNode;
        if (pre.parentNode.classList.contains('highlight')) {
            const highlight = pre.parentNode;
            highlight.parentNode.insertBefore(button, highlight);
        } else {
            pre.parentNode.insertBefore(button, pre);
        }
    });
});
