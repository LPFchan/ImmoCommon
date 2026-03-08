import re

with open('README.md', 'r') as f:
    readme = f.read()

# First replace the header back to "Cost"
readme = readme.replace('| Cost (5 pcs) |', '| Cost  |')
readme = readme.replace('| ------------ |', '| ----- |')

def process_table_row(match):
    row = match.group(0)
    if 'Ref' in row or '---' in row:
        return row
    
    cols = [c.strip() for c in row.split('|')[1:-1]]
    if len(cols) < 5: return row
    
    # Check if the cost starts with $
    cost_str = cols[4]
    if cost_str.startswith('$'):
        try:
            total_cost = float(cost_str[1:])
            # For D3, D4, qty was 10, all others were 5
            # We can map from ref to divisor
            divisor = 10 if cols[0].startswith('D3') else 5
            unit_cost = total_cost / divisor
            cols[4] = f"${unit_cost:.2f}"
            
            # Rebuild row with same padding if possible, or just standard padding
            return f"| {cols[0]:<9} | {cols[1]:<15} | {cols[2]:<65} | {cols[3]:<16} | {cols[4]:<5} |"
        except ValueError:
            pass
            
    return row

table_pattern = re.compile(r'(\| Ref\s+\|.*?)\n\n', re.MULTILINE | re.DOTALL)

def repl_func(m):
    lines = m.group(1).split('\n')
    new_lines = [process_table_row(re.match(r'.*', line)) for line in lines]
    return '\n'.join(new_lines) + '\n\n'

new_readme = table_pattern.sub(repl_func, readme)

with open('README.md', 'w') as f:
    f.write(new_readme)

print("Fixed unit cost")
