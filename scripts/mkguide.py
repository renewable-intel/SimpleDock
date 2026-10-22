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

Every node but the first gets Contents / Prev / Next. In the ReadMe a
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
        if i:
            out.append('@toc main')
            if i > 1:
                out.append('@prev %s' % nodes[i - 1]['name'])
            if i + 1 < len(nodes):
                out.append('@next %s' % nodes[i + 1]['name'])
            out.append('@{b}@{u}%s@{uu}@{ub}' % g_esc(n['title']))
            out.append('')
        for line in n['lines']:
            if line.startswith('== '):
                out.append('@{b}%s@{ub}' % g_esc(line[3:]))
            elif line.startswith('-- '):
                out.append('@{i}%s@{ui}' % g_inline(line[3:], names, n['name']))
            else:
                out.append(g_inline(line, names, n['name']))
        out.append('@endnode')
        out.append('')
    out.append('@node index "Index"')            # every topic, alphabetically (MultiView's Index button)
    out.append('@toc main')
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
           '<title>%s</title>' % html.escape(title),
           '<style>body{font-family:sans-serif;max-width:46em;margin:2em auto;padding:0 1em;line-height:1.45;'
           'color:#222}pre{white-space:pre-wrap;font-family:inherit;margin:0 0 .6em}h2{margin-top:2.2em;'
           'padding-bottom:.2em;border-bottom:1px solid #bbb}h3{margin:1.4em 0 .3em}'
           '.note{color:#666;font-style:italic}a{color:#1a5fb4}</style></head><body>']
    for i, n in enumerate(nodes):
        out.append('<h2 id="%s">%s</h2>' % (n['name'], html.escape(n['title'])))
        block = []
        def flush():
            if block:
                out.append('<pre>%s</pre>' % '\n'.join(block))
                del block[:]
        for line in n['lines']:
            if line.startswith('== '):
                flush()
                out.append('<h3>%s</h3>' % html.escape(line[3:]))
            elif line.startswith('-- '):
                block.append('<span class="note">%s</span>' % h_inline(line[3:]))
            else:
                block.append(h_inline(line))
        flush()
    out.append('</body></html>')
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
        with open(path, 'w', encoding='latin-1', newline='\n') as f:
            f.write(data)
        print('%s: %d lines' % (path, data.count('\n')))


if __name__ == '__main__':
    main()
