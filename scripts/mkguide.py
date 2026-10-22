#!/usr/bin/env python3
"""The user guide, from one source: docs/guide.txt -> dist/SimpleDock.guide
(AmigaGuide, for MultiView), dist/SimpleDock.html, and dist/ReadMe (plain
text, every node in order).

usage: mkguide.py [docs/guide.txt] [dist/]

The source is plain text with a few marks, one short topic per node:

  @node name "Title"      starts a node; the first node is the contents
  @end                    ends it (optional before the next @node)
  {text|node}             a link to a node, shown as "text"
  *text*                  bold (a key name, a setting, a heading word)
  == Heading              a heading inside a node
  -- text                 a dimmed note (AmigaGuide: italic; ReadMe: as is)
  text otherwise          as it is; a blank line is a paragraph break

Topics are linked in reading order, from Contents through the Index. In the ReadMe a
link is its text and bold is plain; a heading is underlined. Lines are
kept as written (wrap at 72 in the source). AmigaGuide's own marks (@
and \\) in the text are escaped."""
import html, os, re, sys

LINK = re.compile(r'\{([^}|]+)\|([^}]+)\}')
BOLD = re.compile(r'\*([^*\n]+)\*')


def parse(src):
    nodes, cur = [], None
    for raw in src.splitlines():
        m = re.match(r'@node\s+(\S+)\s+"([^"]*)"', raw)
        if m:
            cur = {'name': m.group(1), 'title': m.group(2), 'lines': []}
            nodes.append(cur)
            continue
        if raw.strip() == '@end':
            cur = None
            continue
        if cur is not None:
            cur['lines'].append(raw)
    for n in nodes:                      # trailing blank lines off
        while n['lines'] and not n['lines'][-1].strip():
            n['lines'].pop()
    return nodes


def g_esc(s):
    return s.replace('\\', '\\\\').replace('@', '\\@')


def g_inline(s, names, where):
    """Links and bold into AmigaGuide marks; the rest escaped."""
    out, pos = [], 0
    for m in re.finditer(r'\{([^}|]+)\|([^}]+)\}|\*([^*\n]+)\*', s):
        out.append(g_esc(s[pos:m.start()]))
        if m.group(3) is not None:
            out.append('@{b}%s@{ub}' % g_esc(m.group(3)))
        else:
            text, node = m.group(1), m.group(2)
            if node not in names:
                raise SystemExit('no node "%s" (link in %s)' % (node, where))
            out.append('@{"%s" link %s}' % (g_esc(text), node))
        pos = m.end()
    out.append(g_esc(s[pos:]))
    return ''.join(out)


def guide(nodes, title):
    out = ['@database "%s"' % title, '@author "renewable-intel"', '@wordwrap', '@width 76', '@index index', '']
    names = {n['name'] for n in nodes} | {'index'}
    for i, n in enumerate(nodes):
        out.append('@node %s "%s"' % (n['name'], n['title']))
        out.append('@toc main')
        if i:
            out.append('@prev %s' % nodes[i - 1]['name'])
        out.append('@next %s' % (nodes[i + 1]['name'] if i + 1 < len(nodes) else 'index'))
        if i:
            out.append('@{b}@{u}%s@{uu}@{ub}' % g_esc(n['title']))
            out.append('')
        for line in n['lines']:
            if line.startswith('== '):
                if out[-1] and not out[-1].startswith('@next '):
                    out.append('')
                out.append('@{b}%s@{ub}' % g_esc(line[3:]))
            elif line.startswith('-- '):
                out.append('@{i}%s@{ui}' % g_inline(line[3:], names, n['name']))
            else:
                out.append(g_inline(line, names, n['name']))
        out.append('@endnode')
        out.append('')
    out.append('@node index "Index"')            # every topic, alphabetically (MultiView's Index button)
    out.append('@toc main')
    out.append('@prev %s' % nodes[-1]['name'])
    out.append('@next main')
    out.append('@{b}@{u}Index@{uu}@{ub}')
    out.append('')
    for n in sorted(nodes[1:], key=lambda n: n['title'].lower()):
        out.append('  @{"%s" link %s}' % (g_esc(n['title']), n['name']))
    out.append('@endnode')
    out.append('')
    return '\n'.join(out) + '\n'


def plain(s):
    return BOLD.sub(lambda m: m.group(1), LINK.sub(lambda m: m.group(1), s))


def readme(nodes):
    out = []
    for i, n in enumerate(nodes):
        if i:
            out += ['', '', n['title'].upper(), '=' * len(n['title'])]
        for line in n['lines']:
            if line.startswith('== '):
                out += ['', line[3:], '-' * len(line[3:])]
            elif line.startswith('-- '):
                out.append(plain(line[3:]))
            else:
                out.append(plain(line))
    return '\n'.join(out).rstrip('\n') + '\n'


def h_inline(s):
    s = html.escape(s)
    s = LINK.sub(lambda m: '<a href="#%s">%s</a>' % (m.group(2), m.group(1)), s)
    return BOLD.sub(lambda m: '<b>%s</b>' % m.group(1), s)


def html_out(nodes, title):
    out = ['<!DOCTYPE html>', '<html lang="en"><head><meta charset="utf-8">',
           '<meta name="viewport" content="width=device-width, initial-scale=1">',
           '<title>%s</title>' % html.escape(title),
           '''<style>
:root{color-scheme:light;scroll-behavior:smooth}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;max-width:54rem;
margin:0 auto;padding:2.5rem 1.5rem 4rem;line-height:1.65;color:#252b30;
background:#faf9f6}
h1,h2,h3{line-height:1.25;color:#18232c}
h1{font-size:2.3rem;margin:0 0 1rem}
h2{font-size:1.65rem;margin:0 0 1.25rem}
h3{font-size:1.05rem;margin:1.6rem 0 .6rem}
section{border-top:1px solid #d9dcd9;margin-top:2.5rem;padding-top:2rem;
scroll-margin-top:1.5rem}
section:first-child{border:0;margin:0;padding:0}
p{margin:.7rem 0 1rem}
a{color:#235c85;text-underline-offset:.18em}
a:hover{color:#153a55}
pre{white-space:pre-wrap;font: .9rem/1.65 ui-monospace,Menlo,Consolas,monospace;
background:#f0f1ed;border:1px solid #dfe2dc;border-radius:.35rem;
padding:1rem;overflow-wrap:anywhere;margin:.75rem 0 1.25rem}
.note{border-left:3px solid #a6b8bf;padding:.25rem 0 .25rem 1rem;
color:#52616b;font-size:.94rem}
nav{display:flex;flex-wrap:wrap;gap:.5rem 1.25rem;margin-top:1.5rem;
font-size:.9rem}
@media(max-width:600px){body{padding:1.5rem 1rem}pre{font-size:.8rem;padding:.6rem}}
@media print{body{max-width:none;background:white;font-size:10pt}nav{display:none}
section{break-before:auto}h2,h3{break-after:avoid}pre{break-inside:avoid}}
</style></head><body><main>''']
    for i, n in enumerate(nodes):
        out.append('<section id="%s">' % html.escape(n['name'], quote=True))
        tag = 'h1' if i == 0 else 'h2'
        out.append('<%s>%s</%s>' % (tag, html.escape(n['title']), tag))
        block, kind = [], None

        def flush():
            if not block:
                return
            if kind == 'pre':
                # Source tables use spaces for columns: keep a fixed-width font.
                out.append('<pre>%s</pre>' % '\n'.join(h_inline(line) for line in block))
            else:
                attrs = ' class="note"' if kind == 'note' else ''
                out.append('<p%s>%s</p>' % (attrs, h_inline(' '.join(block))))
            block.clear()

        for line in n['lines']:
            if not line.strip():
                flush()
                kind = None
            elif line.startswith('== '):
                flush()
                kind = None
                out.append('<h3>%s</h3>' % html.escape(line[3:]))
            else:
                newkind = 'note' if line.startswith('-- ') else 'pre' if line.startswith(' ') else 'p'
                if newkind != kind:
                    flush()
                    kind = newkind
                block.append(line[3:] if kind == 'note' else line)
        flush()
        if i:
            links = ['<a href="#main">Contents</a>',
                     '<a href="#%s">Previous</a>' % nodes[i - 1]['name']]
            if i + 1 < len(nodes):
                links.append('<a href="#%s">Next</a>' % nodes[i + 1]['name'])
            out.append('<nav aria-label="Topic navigation">%s</nav>' % ' '.join(links))
        out.append('</section>')
    out.append('</main></body></html>')
    return '\n'.join(out) + '\n'

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'docs/guide.txt'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'dist'
    text = open(src, encoding='latin-1').read()
    nodes = parse(text)
    if not nodes or nodes[0]['name'] != 'main':
        raise SystemExit('the first node must be "main" (the contents)')
    title = nodes[0]['title']
    for name, data in (('SimpleDock.guide', guide(nodes, title)),
                       ('ReadMe', readme(nodes)),
                       ('SimpleDock.html', html_out(nodes, title))):
        path = os.path.join(dst, name)
        encoding = 'utf-8' if name.endswith('.html') else 'latin-1'
        with open(path, 'w', encoding=encoding, newline='\n') as f:
            f.write(data)
        print('%s: %d lines' % (path, data.count('\n')))


if __name__ == '__main__':
    main()
