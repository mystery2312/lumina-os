// Lumi dans le navigateur : le texte de la page (ou la selection) est
// transmis a l'appli Lumi de LuminaOS par messagerie native. Rien ne part
// sur Internet : c'est Lumi, sur ce PC, qui repond (sauf si un service en
// ligne a ete choisi dans Reglages > Lumi).
const HOST = "org.luminaos.lumi";
const MAX = 60000;   // caracteres envoyes au plus (Lumi en garde moins)

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({ id: "resumer", title: "Résumer cette page avec Lumi", contexts: ["page"] });
    chrome.contextMenus.create({ id: "question", title: "Poser une question à Lumi sur cette page", contexts: ["page"] });
    chrome.contextMenus.create({ id: "expliquer", title: "Expliquer avec Lumi : « %s »", contexts: ["selection"] });
  });
});

// Texte lisible de la page : l'article ou le contenu principal s'il y en a
// un, sinon tout le corps de la page.
function pageText() {
  const pick = document.querySelector("article, main, [role=main]");
  const t = pick && pick.innerText.trim().length > 400 ? pick.innerText : (document.body ? document.body.innerText : "");
  return t.replace(/\n{3,}/g, "\n\n").trim();
}

async function send(action, tab, text) {
  if (text === undefined) {
    try {
      const [r] = await chrome.scripting.executeScript({ target: { tabId: tab.id }, func: pageText });
      text = r && r.result ? r.result : "";
    } catch (e) {
      text = "";   // page protegee (chrome://, boutique...) : Lumi le dira
    }
  }
  chrome.runtime.sendNativeMessage(HOST, {
    action, title: tab.title || "", url: tab.url || "", text: (text || "").slice(0, MAX)
  }, () => void chrome.runtime.lastError);
}

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (!tab) return;
  if (info.menuItemId === "expliquer") send("expliquer", tab, info.selectionText || "");
  else send(info.menuItemId, tab);
});

chrome.action.onClicked.addListener((tab) => send("resumer", tab));
